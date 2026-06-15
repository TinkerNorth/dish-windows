// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnectionManager.h"

#include "PairingClient.h"
#include "source/connection/DiscoveryGateway.h"
#include "source/connection/LANDiscovery.h"
#include "source/connection/MdnsDiscovery.h"
#include "source/http/SatelliteTlsVerifier.h"
#include "Util/Hex.h"
#include "core/reducer/Backoff.h"
#include "core/reducer/CloseNotify.h"
#include "core/reducer/Reconcile.h"
#include "core/reducer/RestOutcome.h"
#include "core/wire/SessionCrypto.h"

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
// file resolves under one .ts <context> entry.
constexpr const char* kTrContext = "dish::net::WifiConnectionManager";

ConnectionEvent makeError(const QString& msg) { return {ConnectionEventKind::Error, {}, msg}; }

ConnectionEvent pairingRequired(const models::DiscoveredServer& s) {
    return {ConnectionEventKind::PairingRequired, s, {}};
}

// Reduce a descriptor list to the (ctrlIdx, type) pairs the reconcile reducer
// diffs on. Qt→pure boundary, so it lives here rather than in core/reducer.
std::vector<reducer::DesiredSlot>
descriptorsToDesired(const QList<models::ControllerDescriptor>& descriptors) {
    std::vector<reducer::DesiredSlot> out;
    out.reserve(static_cast<std::size_t>(descriptors.size()));
    for (const auto& d : descriptors) {
        out.push_back({static_cast<std::uint8_t>(d.ctrlIdx), d.type});
    }
    return out;
}

QString unreachableMsg() {
    return QCoreApplication::translate(
        kTrContext, "Server unreachable — check it's powered on and on the same Wi-Fi.");
}
QString rePairMsg() {
    return QCoreApplication::translate(
        kTrContext, "This satellite no longer recognizes this device. Re-pair needed.");
}
QString versionMsg() {
    return QCoreApplication::translate(
        kTrContext, "This app and the satellite speak different protocol versions.");
}

} // namespace

WifiConnectionManager::WifiConnectionManager(ConnectionStore* store, QObject* parent)
    : QObject(parent), store_(store), http_(new HTTPClient(this)) {
    deviceId_ = store_->getOrCreateDeviceId();
    deviceName_ = QHostInfo::localHostName();
    if (deviceName_.isEmpty()) { deviceName_ = QStringLiteral("Windows"); }
    // Enforce TOFU cert-pinning on every HTTPS call: pin the cert first seen for
    // a satellite (keyed by host/IP, matching the ConnectionStore pin-migration
    // convention) and reject any later cert whose fingerprint differs. The pin
    // store lives on the ConnectionStore facade; the verifier composes it with
    // core/net/Tofu via source/http/SatelliteTlsVerifier.
    auto& pins = store_->facade().pins();
    http_->setPinVerifier([&pins](const QString& host, const QByteArray& certDer) {
        return http::verifyPeerCertificate(host, pins, certDer);
    });
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
        // DURABLE relearn: a fresh scan can carry a new IP for a remembered
        // satellite (matched by machineId). Persist the moved endpoint back to
        // the remembered store BEFORE anything else, so the next app launch's
        // autoReconnectAll — and any in-flight silent backoff retry, which
        // re-reads store_->remembered() — targets the current address instead of
        // a stale one. Without this, the only path that ever wrote a fresh IP was
        // a SUCCESSFUL session PUT (openSession → store_->remember), which can't
        // happen while the IP is wrong: the "must rescan, then reconnect" trap
        // the user hit. refreshFromDiscovery re-points only already-remembered
        // rows (and migrates the cert pin); it never adds a new satellite.
        // Mirrors dish-android SatelliteConnectionManager.startDiscovery.
        store_->refreshFromDiscovery(discovered_);
        // A fresh scan can carry a new IP for a remembered satellite (same
        // machineId) — refresh the live connection's server so the next connect
        // targets the current address.
        for (const auto& server : discovered_) {
            if (auto* conn = connections_.value(server.id(), nullptr)) {
                if (conn->state() == SessionState::Idle || conn->state() == SessionState::Stale) {
                    conn->updateServer(server);
                }
            }
        }
        // A relearned endpoint should also (re)attempt any remembered satellite
        // that isn't currently live — so a moved box reconnects on its own once
        // the scan finds it, with no manual Connect. Silent intent: the row
        // chip is the cue, and a still-stale entry just rides the backoff curve.
        autoReconnectAll();
        emit discoveredChanged();
        emit scanningChanged();
        if (discovered_.isEmpty()) {
            emit connectionEvent(makeError(
                QCoreApplication::translate(kTrContext, "No servers found — check your network")));
        }
        watcher->deleteLater();
    });
    watcher->setFuture(QtConcurrent::run([] {
        auto mdnsFuture = QtConcurrent::run([] { return MdnsDiscovery::discover(); });
        const QList<models::DiscoveredServer> beacon = LANDiscovery::discover();
        const QList<models::DiscoveredServer> mdns = mdnsFuture.result();
        const QList<models::DiscoveredServer> merged =
            DiscoveryGateway::mergeDiscovered(beacon, mdns);
        qInfo("discovery scan: broadcast=%lld mdns=%lld merged=%lld",
              static_cast<long long>(beacon.size()), static_cast<long long>(mdns.size()),
              static_cast<long long>(merged.size()));
        return merged;
    }));
}

void WifiConnectionManager::wireSlotSync(WifiConnection* conn) {
    const QString id = conn->id();
    // A descriptor change / removal while live converges via the per-controller
    // REST routes (the session/UDP keys never churn for a toggle).
    QObject::connect(conn, &WifiConnection::slotChanged, this,
                     [this, id](const QString& slotId) { syncSlot(id, slotId); });
    QObject::connect(conn, &WifiConnection::slotRemoved, this,
                     [this, id](int ctrlIdx) { deleteSlot(id, ctrlIdx); });
}

WifiConnection* WifiConnectionManager::ensureConnection(const models::DiscoveredServer& server) {
    const auto id = WifiConnection::idFor(server);
    if (auto* existing = connections_.value(id, nullptr)) { return existing; }
    auto* conn = new WifiConnection(id, server, this);
    connections_.insert(id, conn);
    QObject::connect(conn, &WifiConnection::changed, this, &WifiConnectionManager::poolChanged);
    QObject::connect(conn, &WifiConnection::errorOccurred, this,
                     [this](const QString& msg) { emit connectionEvent(makeError(msg)); });
    wireSlotSync(conn);
    emit poolChanged();
    return conn;
}

std::optional<WifiConnectionManager::Credentials>
WifiConnectionManager::credentialsFor(const QString& id) const {
    const auto keyHex = store_->sharedKey(id);
    if (!keyHex.has_value() || keyHex->size() != 64) { return std::nullopt; }
    const auto keyBytes = util::fromHex(keyHex->toStdString());
    if (!keyBytes || keyBytes->size() != 32) { return std::nullopt; }
    Credentials creds;
    std::copy_n(keyBytes->begin(), 32, creds.pairingKey.begin());
    creds.proof = QString::fromStdString(
        wire::computeHmacProof(creds.pairingKey.data(), deviceId_.toStdString()));
    return creds;
}

void WifiConnectionManager::connectTo(const models::DiscoveredServer& server,
                                      ConnectIntent intent) {
    auto* conn = ensureConnection(server);
    if (intent == ConnectIntent::UserInitiated) { retryAttempts_.remove(conn->id()); }
    if (conn->state() == SessionState::Live || conn->state() == SessionState::Linking) {
        conn->updateServer(server);
        return;
    }
    conn->updateServer(server);
    conn->markConnecting();
    // Skip the pair handshake if we already hold a pairing key: a moved/offline
    // server then fails fast in the session PUT instead of bouncing through
    // pair → PairingRequired and trapping the user behind a PIN prompt.
    if (credentialsFor(conn->id()).has_value()) {
        openSession(conn, server, intent);
    } else {
        pairAndConnect(conn, server, intent);
    }
}

void WifiConnectionManager::pairWithPin(const models::DiscoveredServer& server,
                                        const QString& pin) {
    auto* conn = ensureConnection(server);
    retryAttempts_.remove(conn->id());
    if (conn->state() == SessionState::Live) { return; }
    conn->updateServer(server);
    conn->markConnecting();

    const QString did = deviceId_;
    const QString dname = deviceName_;
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    auto* watcher = new QFutureWatcher<models::PairResponse>(this);
    QObject::connect(
        watcher, &QFutureWatcherBase::finished, this, [this, watcher, conn, server, pin] {
            const auto pair = watcher->result();
            watcher->deleteLater();
            pairingInFlight_.remove(conn->id());
            emit pairingInFlightChanged();
            const auto outcome = PairingClient::classify(pair);
            std::visit(
                [&](auto&& arm) {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, PairingClient::Success>) {
                        store_->setSharedKey(arm.sharedKeyHex, WifiConnection::idFor(server));
                        openSession(conn, server, ConnectIntent::UserInitiated);
                    } else if constexpr (std::is_same_v<T, PairingClient::VersionMismatch>) {
                        conn->markDisconnected();
                        emit connectionEvent(makeError(versionMsg()));
                    } else {
                        // A PIN submit that comes back Pending/AuthRequired/
                        // Unreachable is a failed pair — surface the reason.
                        conn->markDisconnected();
                        emit connectionEvent(makeError(pair.error.value_or(
                            QCoreApplication::translate(kTrContext, "Pairing failed"))));
                    }
                },
                outcome);
        });
    watcher->setFuture(QtConcurrent::run([server, did, dname, pin] {
        return PairingClient::pair(server.ip, server.pairPort, did, dname, pin);
    }));
}

void WifiConnectionManager::pairAndConnect(WifiConnection* conn,
                                           const models::DiscoveredServer& server,
                                           ConnectIntent intent) {
    const QString did = deviceId_;
    const QString dname = deviceName_;
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    auto* watcher = new QFutureWatcher<models::PairResponse>(this);
    QObject::connect(
        watcher, &QFutureWatcherBase::finished, this, [this, watcher, conn, server, intent] {
            const auto pair = watcher->result();
            watcher->deleteLater();
            pairingInFlight_.remove(conn->id());
            emit pairingInFlightChanged();
            const auto outcome = PairingClient::classify(pair);
            std::visit(
                [&](auto&& arm) {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, PairingClient::Success>) {
                        store_->setSharedKey(arm.sharedKeyHex, WifiConnection::idFor(server));
                        openSession(conn, server, intent);
                    } else if constexpr (std::is_same_v<T, PairingClient::AuthRequired> ||
                                         std::is_same_v<T, PairingClient::Pending>) {
                        // First-time pair / server forgot us. UserInitiated →
                        // the PIN prompt; silent intents land in Stale ("Needs
                        // pairing") so the next user tap gets the dialog.
                        if (intent == ConnectIntent::UserInitiated) {
                            conn->markDisconnected();
                            emit connectionEvent(pairingRequired(server));
                        } else {
                            conn->markStale();
                        }
                    } else if constexpr (std::is_same_v<T, PairingClient::VersionMismatch>) {
                        conn->markDisconnected();
                        emitErrorIfUserInitiated(intent, versionMsg());
                    } else {
                        // Unreachable: silent intents rely on the chip; a user
                        // tap gets the explicit toast.
                        if (intent == ConnectIntent::UserInitiated) {
                            conn->markDisconnected();
                            emit connectionEvent(makeError(unreachableMsg()));
                        } else {
                            conn->markStale();
                        }
                    }
                },
                outcome);
        });
    watcher->setFuture(QtConcurrent::run([server, did, dname] {
        return PairingClient::pair(server.ip, server.pairPort, did, dname, QString());
    }));
}

void WifiConnectionManager::openSession(WifiConnection* conn,
                                        const models::DiscoveredServer& server,
                                        ConnectIntent intent) {
    const QString id = conn->id();
    auto creds = credentialsFor(id);
    if (!creds.has_value()) {
        // No usable key — drop it and surface re-pair.
        onTerminalAuthFailure(conn, id, intent);
        return;
    }
    const QString proof = creds->proof;
    const auto descriptors = conn->desiredDescriptors();
    const bool wantsMouse = conn->wantsMouseControl();
    const auto pairingKey = creds->pairingKey;
    // Snapshot the (ctrlIdx, type) set we are about to send, so the response
    // callback can converge any slot changes that race the round-trip.
    const auto sentDescriptors = descriptorsToDesired(descriptors);

    http_->putSession(
        server.ip, server.httpPort, deviceId_, deviceName_, proof, descriptors, wantsMouse,
        [this, conn, server, intent, id, pairingKey,
         sentDescriptors](const models::SessionResponse& resp) {
            using reducer::RestVerdict;
            reducer::RestReply rr;
            rr.status = resp.httpStatus;
            rr.bodyParsed = resp.reachable;
            rr.code = resp.code.value_or(QString()).toStdString();
            const RestVerdict verdict = classifyRest(rr);
            if (verdict == RestVerdict::Unauthorized) {
                onTerminalAuthFailure(conn, id, intent);
                return;
            }
            if (verdict == RestVerdict::VersionMismatch) {
                conn->markDisconnected();
                emitErrorIfUserInitiated(intent, versionMsg());
                return;
            }
            if (verdict != RestVerdict::Ok || !resp.connectionId || !resp.token ||
                !resp.sessionSalt) {
                // Unreachable / 503 / malformed: park (stale on silent intents)
                // and schedule a backoff retry.
                if (intent == ConnectIntent::UserInitiated) {
                    conn->markDisconnected();
                    emit connectionEvent(makeError(unreachableMsg()));
                } else {
                    conn->markStale();
                }
                scheduleRetry(server, intent);
                return;
            }
            // Decode token (4B) + salt (8B); malformed degrades like a refused
            // connect rather than crashing.
            const auto tok = util::fromHex(resp.token->toStdString());
            const auto salt = util::fromHex(resp.sessionSalt->toStdString());
            if (!tok || tok->size() != 4 || !salt || salt->size() != 8) {
                conn->markDisconnected();
                return;
            }
            std::array<std::uint8_t, 4> token{};
            std::copy_n(tok->begin(), 4, token.begin());
            std::array<std::uint8_t, 8> saltArr{};
            std::copy_n(salt->begin(), 8, saltArr.begin());
            // Per-session key: the pairing key never touches the UDP path.
            const std::uint32_t tokenBe = (static_cast<std::uint32_t>(token[0]) << 24) |
                                          (static_cast<std::uint32_t>(token[1]) << 16) |
                                          (static_cast<std::uint32_t>(token[2]) << 8) |
                                          static_cast<std::uint32_t>(token[3]);
            std::array<std::uint8_t, 32> sessionKey{};
            wire::deriveSessionKey(pairingKey.data(), saltArr.data(), tokenBe, sessionKey.data());

            auto client = std::make_shared<SatelliteClient>();
            if (!client->openSocket(server.ip.toStdString(), server.udpPort)) {
                conn->markDisconnected();
                return;
            }
            client->setConnectionParams(token, sessionKey);
            store_->remember(server);
            retryAttempts_.remove(id);

            conn->markConnected(
                client, *resp.connectionId, resp.epoch, resp.mouseControl.granted,
                /*onDead=*/
                [this, id, server] {
                    disconnect(id);
                    scheduleRetry(server, ConnectIntent::RetryAfterDeath);
                },
                /*onClose=*/
                [this, id, server](std::uint8_t reason) {
                    if (auto* c = connections_.value(id, nullptr)) {
                        handleServerClose(c, server, reason);
                    }
                },
                /*onReconcile=*/
                [this, id, server] {
                    if (auto* c = connections_.value(id, nullptr)) { reconcile(c, server); }
                });
            // Fold the PUT's apply results into the slot state (registers the
            // live slots; surfaces failures).
            conn->applyResults(resp.controllers);
            // Converge any slot changes that raced the response.
            const auto converge = reducer::lateSlotConverge(
                sentDescriptors, descriptorsToDesired(conn->desiredDescriptors()));
            for (std::uint8_t ctrlIdx : converge.removes) { deleteSlot(id, ctrlIdx); }
            for (std::uint8_t ctrlIdx : converge.resyncs) {
                const QString slotId = conn->slotIdForIndex(ctrlIdx);
                if (!slotId.isEmpty()) { syncSlot(id, slotId); }
            }
        });
}

void WifiConnectionManager::reconcile(WifiConnection* conn,
                                      const models::DiscoveredServer& server) {
    const QString id = conn->id();
    if (conn->state() != SessionState::Live) { return; }
    const auto connId = conn->connectionId();
    if (!connId.has_value()) { return; }
    auto client = conn->client();
    if (!client) { return; }
    // Only reconcile when the enriched ack actually drifted from applied.
    if (!reducer::reconcileNeeded(client->serverEpoch(), client->serverBitmap(),
                                  conn->lastAppliedEpoch(), conn->registeredBitmap())) {
        return;
    }
    if (reconcileInFlight_.contains(id)) { return; }
    reconcileInFlight_.insert(id);
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) {
        reconcileInFlight_.remove(id);
        return;
    }
    http_->getSession(server.ip, server.httpPort, *connId, deviceId_, creds->proof,
                      [this, id, server](const models::SessionViewDto& view) {
                          reconcileInFlight_.remove(id);
                          auto* c = connections_.value(id, nullptr);
                          if (c == nullptr || c->state() != SessionState::Live) { return; }
                          if (view.unauthorized()) {
                              onTerminalAuthFailure(c, id, ConnectIntent::RetryAfterDeath);
                              return;
                          }
                          if (!view.reachable || view.httpStatus < 200 || view.httpStatus > 299) {
                              return;
                          }
                          if (view.connectionId == c->connectionId() &&
                              c->matchesAppliedView(view)) {
                              // Benign drift (e.g. our own standalone PUT raced an ack):
                              // just adopt the epoch.
                              c->adoptEpoch(view.epoch);
                              return;
                          }
                          // Applied ≠ desired (or the session is gone): converge with a fresh
                          // session PUT. Tear the UDP tuple down first — the PUT rotates
                          // token/key.
                          c->markDisconnected();
                          c->markConnecting();
                          openSession(c, server, ConnectIntent::RetryAfterDeath);
                      });
}

void WifiConnectionManager::syncSlot(const QString& id, const QString& slotId) {
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr || conn->state() != SessionState::Live) { return; }
    const auto connId = conn->connectionId();
    if (!connId.has_value()) { return; }
    const auto descriptor = conn->descriptorFor(slotId);
    if (!descriptor.has_value()) { return; }
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) { return; }
    const models::DiscoveredServer server = conn->server();
    http_->putController(server.ip, server.httpPort, *connId, deviceId_, creds->proof, *descriptor,
                         [this, id, server](const models::ControllerPutResponse& resp) {
                             auto* c = connections_.value(id, nullptr);
                             if (c == nullptr) { return; }
                             if (resp.unauthorized()) {
                                 onTerminalAuthFailure(c, id, ConnectIntent::RetryAfterDeath);
                                 return;
                             }
                             if (!resp.controller.has_value()) {
                                 // 404 connection-not-found: the session died under us; the
                                 // alive-poll / close-notify path owns recovery.
                                 return;
                             }
                             c->adoptEpoch(resp.epoch);
                             c->applyResults(QList<models::ControllerApplyDto>{*resp.controller});
                             // A mouse-mode toggle changed the session-level desire, but the
                             // grant is only computed at session PUT — converge the full session.
                             if (c->wantsMouseControl() != c->mouseControlGranted()) {
                                 reconcile(c, server);
                             }
                         });
}

void WifiConnectionManager::deleteSlot(const QString& id, int ctrlIdx) {
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto connId = conn->connectionId();
    if (!connId.has_value()) { return; }
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) { return; }
    const models::DiscoveredServer server = conn->server();
    http_->deleteController(server.ip, server.httpPort, *connId, ctrlIdx, deviceId_, creds->proof,
                            [this, id](const models::ControllerPutResponse& resp) {
                                auto* c = connections_.value(id, nullptr);
                                if (c == nullptr) { return; }
                                if (!resp.error.has_value()) { c->adoptEpoch(resp.epoch); }
                            });
}

void WifiConnectionManager::handleServerClose(WifiConnection* conn,
                                              const models::DiscoveredServer& server,
                                              std::uint8_t reason) {
    const QString id = conn->id();
    switch (reducer::closeActionForReason(reason)) {
    case reducer::CloseAction::DropKeyRePair:
        // unpaired: trust revoked — drop the key, park Stale, stop retrying.
        conn->markStale();
        store_->forgetKey(id);
        break;
    case reducer::CloseAction::StayDown:
        // replaced: a newer PUT already owns the session.
        conn->markDisconnected();
        break;
    case reducer::CloseAction::RetryBackoff:
        // shutdown / kicked: transient — reconnect on the backoff curve.
        conn->markDisconnected();
        scheduleRetry(server, ConnectIntent::RetryAfterDeath);
        break;
    }
}

void WifiConnectionManager::scheduleRetry(const models::DiscoveredServer& server,
                                          ConnectIntent intent) {
    if (intent == ConnectIntent::UserInitiated) { return; }
    const QString id = server.id();
    const int attempt = retryAttempts_.value(id, 0) + 1;
    retryAttempts_.insert(id, attempt);
    const auto delay = reducer::backoffDelayMs(attempt);
    QTimer::singleShot(static_cast<int>(delay), this, [this, id, server] {
        auto* c = connections_.value(id, nullptr);
        if (c == nullptr) { return; }
        // Only retry from a settled-down state — a user-driven reconnect or
        // forget in the interim moved it out, and we don't want to clobber that.
        if (c->state() != SessionState::Idle && c->state() != SessionState::Stale) { return; }
        // Relearn a moved endpoint WITHOUT the user having to open Manage and
        // press Scan: kick a discovery pass (idempotent — a no-op if one is
        // already running). On completion it persists any new IP via
        // refreshFromDiscovery and re-runs autoReconnectAll, so a box that moved
        // DHCP leases reconnects on its own. We ALSO attempt the freshest
        // last-known endpoint right now (below), so a satellite that discovery
        // can't reach — e.g. mDNS/broadcast blocked on the segment — still gets a
        // direct backoff attempt rather than waiting on a scan that may find
        // nothing.
        startDiscovery();
        // Prefer the freshest remembered endpoint (a re-scan may have a new IP).
        models::DiscoveredServer target = server;
        for (const auto& r : store_->remembered()) {
            if (r.id == id) {
                target = r.toDiscovered();
                break;
            }
        }
        connectTo(target, ConnectIntent::RetryAfterDeath);
    });
}

void WifiConnectionManager::onTerminalAuthFailure(WifiConnection* conn, const QString& id,
                                                  ConnectIntent intent) {
    // 401 NOT_PAIRED / BAD_PROOF (or no usable key): terminal by contract.
    // Drop the key, park Stale ("Needs pairing"), and STOP the retry curve.
    conn->markStale();
    store_->forgetKey(id);
    retryAttempts_.remove(id);
    emitErrorIfUserInitiated(intent, rePairMsg());
}

void WifiConnectionManager::emitErrorIfUserInitiated(ConnectIntent intent, const QString& message) {
    if (intent == ConnectIntent::UserInitiated) { emit connectionEvent(makeError(message)); }
}

void WifiConnectionManager::markStale(const QString& id) {
    if (auto* conn = connections_.value(id, nullptr)) { conn->markStale(); }
}

void WifiConnectionManager::disconnect(const QString& id) {
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto server = conn->server();
    const auto cid = conn->connectionId();
    const auto creds = credentialsFor(id);
    conn->markDisconnected();
    // Graceful server-side close (authed). Best-effort — the closer already
    // knows the session is gone.
    if (cid.has_value() && creds.has_value()) {
        http_->deleteSession(server.ip, server.httpPort, *cid, deviceId_, creds->proof,
                             [](int, bool, const QString&) {});
    }
}

void WifiConnectionManager::forget(const QString& id) {
    auto* conn = connections_.value(id, nullptr);
    // Self-unpair BEFORE dropping the key (the proof needs it): the satellite
    // closes any live session and drops its trust row, so a forgotten dish can't
    // leave a paired ghost server-side.
    if (conn != nullptr) {
        const auto server = conn->server();
        const auto creds = credentialsFor(id);
        if (creds.has_value()) {
            http_->unpair(server.ip, server.httpPort, deviceId_, creds->proof,
                          [](int, bool, const QString&) {});
        }
    }
    disconnect(id);
    store_->forget(id);
    retryAttempts_.remove(id);
    reconcileInFlight_.remove(id);
    if (auto* taken = connections_.take(id)) {
        taken->deleteLater();
        emit poolChanged();
    }
}

void WifiConnectionManager::autoReconnectAll() {
    for (const auto& r : store_->remembered()) {
        auto* existing = connections_.value(r.id, nullptr);
        if (existing == nullptr || existing->state() != SessionState::Live) {
            connectTo(r.toDiscovered(), ConnectIntent::AutoReconnect);
        }
    }
}

} // namespace dish::net
