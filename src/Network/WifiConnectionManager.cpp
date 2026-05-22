// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnectionManager.h"

#include "LANDiscovery.h"
#include "MdnsDiscovery.h"
#include "PairingClient.h"
#include "Util/Hex.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QtGlobal>

#include <type_traits>
#include <variant>

namespace dish::net {

namespace {

// Single stable translation context so every user-facing error string in this
// file resolves under one .ts <context> entry. WifiConnectionManager DOES
// inherit Q_OBJECT, but several of the error strings below are emitted from
// captured lambdas / detached helpers where `tr()` isn't in scope; for
// consistency we route them all through QCoreApplication::translate.
constexpr const char* kTrContext = "dish::net::WifiConnectionManager";

ConnectionEvent makeError(const QString& msg) { return {ConnectionEventKind::Error, {}, msg}; }

ConnectionEvent makeWarning(const QString& msg) { return {ConnectionEventKind::Warning, {}, msg}; }

ConnectionEvent pairingRequired(const models::DiscoveredServer& s) {
    return {ConnectionEventKind::PairingRequired, s, {}};
}

// Delay before the alive-poll's onDead path attempts a silent reconnect.
// Short enough that a momentary Wi-Fi drop self-heals before the user
// navigates away in frustration; long enough that a real outage doesn't
// burn the satellite's TCP/UDP buffers with back-to-back retries. Mirrors
// dish-android's AUTO_RETRY_BACKOFF_MS.
constexpr int kSilentRetryBackoffMs = 1500;

} // namespace

WifiConnectionManager::WifiConnectionManager(ConnectionStore* store, QObject* parent)
    : QObject(parent), store_(store), http_(new HTTPClient(this)) {
    deviceId_ = store_->getOrCreateDeviceId();
    deviceName_ = QHostInfo::localHostName();
    if (deviceName_.isEmpty()) { deviceName_ = QStringLiteral("Windows"); }
}

WifiConnectionManager::~WifiConnectionManager() {
    for (auto* c : connections_) { c->markDisconnected(); }
}

void WifiConnectionManager::startDiscovery() {
    if (scanning_) { return; }
    scanning_ = true;
    emit scanningChanged();
    auto* watcher = new QFutureWatcher<QList<models::DiscoveredServer>>(this);
    QObject::connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher] {
        discovered_ = watcher->result();
        scanning_ = false;
        emit discoveredChanged();
        emit scanningChanged();
        if (discovered_.isEmpty()) {
            emit connectionEvent(makeError(
                QCoreApplication::translate(kTrContext, "No servers found — check your network")));
        }
        watcher->deleteLater();
    });
    // Two discovery paths in parallel, merged by ip:udpPort: the legacy UDP
    // broadcast beacon and mDNS / Bonjour. mDNS reaches subnets that drop
    // broadcast; the beacon stays as the fallback for pre-responder
    // satellites. The mDNS scan runs on a second pool thread so the combined
    // wall time is one timeout, not two.
    watcher->setFuture(QtConcurrent::run([] {
        auto mdnsFuture = QtConcurrent::run([] { return MdnsDiscovery::discover(); });
        const QList<models::DiscoveredServer> beacon = LANDiscovery::discover();
        const QList<models::DiscoveredServer> mdns = mdnsFuture.result();
        const QList<models::DiscoveredServer> merged = mergeDiscovered(beacon, mdns);
        // Per-path discovery logging so the broadcast vs mDNS hit-rate can be
        // compared in the field (Task 1.6).
        qInfo("discovery scan: broadcast=%lld mdns=%lld merged=%lld",
              static_cast<long long>(beacon.size()), static_cast<long long>(mdns.size()),
              static_cast<long long>(merged.size()));
        return merged;
    }));
}

WifiConnection* WifiConnectionManager::ensureConnection(const models::DiscoveredServer& server) {
    const auto id = WifiConnection::idFor(server);
    if (auto* existing = connections_.value(id, nullptr)) { return existing; }
    auto* conn = new WifiConnection(id, server, this);
    connections_.insert(id, conn);
    QObject::connect(conn, &WifiConnection::changed, this, &WifiConnectionManager::poolChanged);
    QObject::connect(conn, &WifiConnection::errorOccurred, this,
                     [this](const QString& msg) { emit connectionEvent(makeError(msg)); });
    QObject::connect(conn, &WifiConnection::motionDeliveryWarning, this,
                     [this](const QString& msg) { emit connectionEvent(makeWarning(msg)); });
    QObject::connect(conn, &WifiConnection::registrationFailed, this,
                     &WifiConnectionManager::slotRegistrationFailed);
    emit poolChanged();
    return conn;
}

void WifiConnectionManager::connectTo(const models::DiscoveredServer& server,
                                      ConnectIntent intent) {
    auto* conn = ensureConnection(server);
    if (conn->state() == SessionState::Live || conn->state() == SessionState::Linking) {
        conn->updateServer(server);
        return;
    }
    conn->updateServer(server);
    conn->markConnecting();
    pairAndConnect(conn, server, QString(), intent);
}

void WifiConnectionManager::pairWithPin(const models::DiscoveredServer& server,
                                        const QString& pin) {
    auto* conn = ensureConnection(server);
    conn->markConnecting();
    // A PIN submission is always user-initiated by definition.
    pairAndConnect(conn, server, pin, ConnectIntent::UserInitiated);
}

void WifiConnectionManager::pairAndConnect(WifiConnection* conn,
                                           const models::DiscoveredServer& server,
                                           const QString& pin, ConnectIntent intent) {
    // Auto-reconnect fast path (pin.isEmpty()): if we already have a shared
    // key saved for this server, skip the TCP pair handshake entirely and
    // go straight to openSession. A moved/offline server then fails fast in
    // the HTTP layer instead of bouncing through pair → PairingRequired and
    // trapping the user behind a PIN prompt that can't be satisfied. Mirrors
    // dish-android PR #43.
    if (pin.isEmpty()) {
        const auto saved = store_->sharedKey(WifiConnection::idFor(server));
        if (saved.has_value() && saved->size() == 64) {
            openSession(conn, server, intent);
            return;
        }
    }
    const QString did = deviceId_;
    const QString dname = deviceName_;
    // Mark the connection as "pair in flight" before the future starts so
    // the UI's spinner appears the moment the user clicks Connect / Pair.
    // The set is cleared in every terminal branch below — including the
    // openSession path, which only finishes the in-flight signal once the
    // session is actually live (or has hard-failed). Mirrors dish-mac's
    // `pairingInFlight.insert + defer remove` pattern in
    // WifiConnectionManager.swift, but split across the async hops.
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    auto* watcher = new QFutureWatcher<models::PairResponse>(this);
    QObject::connect(
        watcher, &QFutureWatcherBase::finished, this, [this, watcher, conn, server, pin, intent] {
            const auto pair = watcher->result();
            watcher->deleteLater();
            const auto outcome = PairingClient::classify(pair);
            std::visit(
                [&](auto&& arm) {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, PairingClient::Success>) {
                        store_->setSharedKey(arm.sharedKeyHex, WifiConnection::idFor(server));
                        // openSession is responsible for clearing
                        // pairingInFlight_ at its own terminal points so the
                        // spinner stays visible right through the HTTP
                        // connect handshake.
                        openSession(conn, server, intent);
                    } else if constexpr (std::is_same_v<T, PairingClient::AuthRequired>) {
                        // Server forgot us / rotated keys. On a user-initiated
                        // attempt this is the natural prompt-for-PIN moment.
                        // On AutoReconnect / RetryAfterDeath we MUST stay
                        // silent — popping a dialog the user didn't ask for
                        // every cold start is hostile. Instead, land in Stale
                        // so the row chip reads "Needs pairing" and the user's
                        // next explicit Connect tap will get the dialog.
                        pairingInFlight_.remove(conn->id());
                        emit pairingInFlightChanged();
                        if (intent == ConnectIntent::UserInitiated) {
                            conn->markDisconnected();
                            if (pin.isEmpty()) {
                                emit connectionEvent(pairingRequired(server));
                            } else {
                                emit connectionEvent(makeError(pair.error.value_or(
                                    QCoreApplication::translate(kTrContext, "Pairing failed"))));
                            }
                        } else {
                            conn->markStale();
                        }
                    } else if constexpr (std::is_same_v<T, PairingClient::Unreachable>) {
                        // For an unreachable server, AutoReconnect/RetryAfterDeath
                        // should be silent (the chip handles the narrative)
                        // and UserInitiated gets the explicit toast.
                        pairingInFlight_.remove(conn->id());
                        emit pairingInFlightChanged();
                        if (intent == ConnectIntent::UserInitiated) {
                            conn->markDisconnected();
                            emit connectionEvent(makeError(
                                QCoreApplication::translate(
                                    kTrContext, "Server unreachable — has it moved networks? (%1)")
                                    .arg(arm.message)));
                        } else {
                            // We still hold a (presumably valid) shared key,
                            // so a future scan-and-connect will pick this back
                            // up; Stale matches the "Needs pairing" / row-only
                            // narrative the Android client uses.
                            conn->markStale();
                        }
                    }
                },
                outcome);
        });
    watcher->setFuture(QtConcurrent::run([server, did, dname, pin] {
        return PairingClient::pair(server.ip, server.pairPort, did, dname, pin);
    }));
}

void WifiConnectionManager::openSession(WifiConnection* conn,
                                        const models::DiscoveredServer& server,
                                        ConnectIntent intent) {
    const auto id = WifiConnection::idFor(server);
    // Helper: every terminal branch of openSession must clear the in-flight
    // signal so the UI's spinner doesn't stick. Captured in the
    // http_->connectAsync callback below.
    auto clearInFlight = [this](const QString& cid) {
        if (pairingInFlight_.remove(cid)) { emit pairingInFlightChanged(); }
    };
    // Helper: park the connection in Stale (silent path) or Idle (user-driven
    // path) according to intent. Stale keeps the row chip on "Needs pairing"
    // so the user discovers the problem at the chip, not via a popup they
    // never asked for.
    auto parkOnFailure = [intent](WifiConnection* c) {
        if (intent == ConnectIntent::UserInitiated) {
            c->markDisconnected();
        } else {
            c->markStale();
        }
    };
    const auto keyHex = store_->sharedKey(id);
    if (!keyHex.has_value() || keyHex->size() != 64) {
        parkOnFailure(conn);
        clearInFlight(conn->id());
        emitErrorIfUserInitiated(
            intent, QCoreApplication::translate(kTrContext, "No shared key — re-pair needed"));
        return;
    }
    const auto keyBytes = util::fromHex(keyHex->toStdString());
    if (!keyBytes || keyBytes->size() != 32) {
        parkOnFailure(conn);
        clearInFlight(conn->id());
        emitErrorIfUserInitiated(
            intent, QCoreApplication::translate(kTrContext, "Bad shared key — re-pair needed"));
        return;
    }
    std::array<std::uint8_t, 32> key{};
    std::copy_n(keyBytes->begin(), 32, key.begin());

    http_->connectAsync(
        server.ip, server.httpPort, deviceId_,
        [this, conn, server, key, clearInFlight, intent,
         parkOnFailure](const models::ConnectResponse& resp) {
            if (!resp.connectionId.has_value() || !resp.token.has_value()) {
                parkOnFailure(conn);
                clearInFlight(conn->id());
                emitErrorIfUserInitiated(
                    intent, QCoreApplication::translate(kTrContext, "Error: %1")
                                .arg(resp.error.value_or(
                                    QCoreApplication::translate(kTrContext, "connection failed"))));
                return;
            }
            const auto tok = util::fromHex(resp.token->toStdString());
            if (!tok || tok->size() != 4) {
                parkOnFailure(conn);
                clearInFlight(conn->id());
                emitErrorIfUserInitiated(
                    intent, QCoreApplication::translate(kTrContext, "Bad token from server"));
                return;
            }
            std::array<std::uint8_t, 4> token{};
            std::copy_n(tok->begin(), 4, token.begin());

            auto client = std::make_shared<SatelliteClient>();
            if (!client->openSocket(server.ip.toStdString(), server.udpPort)) {
                parkOnFailure(conn);
                clearInFlight(conn->id());
                return;
            }
            client->setConnectionParams(token, key);
            store_->remember(server);
            const QString cid = *resp.connectionId;
            const QString connId = conn->id();
            // onDead: the alive-poll says the heartbeat ACK stream stopped.
            // Instead of jumping straight to disconnect (which would surface
            // as an opaque "Offline" chip and lose the "I still have a valid
            // key" signal), schedule a silent retry. The first retry runs
            // RETRY_AFTER_DEATH so all of its failure paths stay silent — the
            // user sees one "Connecting…" flicker, not a dialog.
            conn->markConnected(client, cid, [this, connId] { scheduleSilentRetry(connId); });
            // Session is live — pairing flow is fully resolved.
            clearInFlight(connId);
        });
}

void WifiConnectionManager::emitErrorIfUserInitiated(ConnectIntent intent, const QString& message) {
    if (intent == ConnectIntent::UserInitiated) { emit connectionEvent(makeError(message)); }
}

void WifiConnectionManager::scheduleSilentRetry(const QString& id) {
    // Snapshot the server + reset the live session first. markStale leaves the
    // row in "Needs pairing" until the retry lands a fresh Live state, at
    // which point markConnected promotes it back to Live. We capture id by
    // value so a forget() between now and the timeout doesn't dereference a
    // freed pointer — connectTo / get tolerate a missing entry just fine.
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto server = conn->server();
    conn->markStale();
    QTimer::singleShot(kSilentRetryBackoffMs, this, [this, id, server] {
        auto* c = connections_.value(id, nullptr);
        if (c == nullptr) { return; }
        // Only retry if we're still in the post-onDead state we set above —
        // a user-driven reconnect or forget in the interim would have moved
        // the connection out of Stale, and we don't want to clobber that.
        if (c->state() != SessionState::Stale) { return; }
        connectTo(server, ConnectIntent::RetryAfterDeath);
    });
}

void WifiConnectionManager::disconnect(const QString& id) {
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto server = conn->server();
    const auto cid = conn->connectionId();
    conn->markDisconnected();
    if (cid.has_value()) {
        http_->disconnectAsync(server.ip, server.httpPort, *cid, deviceId_,
                               [](const models::ConnectResponse&) {});
    }
}

void WifiConnectionManager::forget(const QString& id) {
    disconnect(id);
    store_->forget(id);
    if (auto* conn = connections_.take(id)) {
        conn->deleteLater();
        emit poolChanged();
    }
}

void WifiConnectionManager::autoReconnectAll() {
    for (const auto& r : store_->remembered()) {
        auto* existing = connections_.value(r.id, nullptr);
        if (existing == nullptr || existing->state() != SessionState::Live) {
            // Tag every implicit reconnect as AutoReconnect so its failure
            // paths stay silent — the row chip handles the narrative.
            connectTo(r.toDiscovered(), ConnectIntent::AutoReconnect);
        }
    }
}

} // namespace dish::net
