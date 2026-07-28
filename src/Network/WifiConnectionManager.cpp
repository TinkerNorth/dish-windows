// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnectionManager.h"

#include "PairingClient.h"
#include "core/net/IpLiterals.h"
#include "source/connection/DiscoveryGateway.h"
#include "source/connection/LANDiscovery.h"
#include "source/connection/MdnsDiscovery.h"
#include "source/http/SatelliteTlsVerifier.h"
#include "Util/Hex.h"
#include "core/reducer/Backoff.h"
#include "core/reducer/CloseNotify.h"
#include "core/reducer/Reconcile.h"
#include "core/reducer/RestOutcome.h"
#include "core/reducer/ReversePairing.h"
#include "core/wire/SessionCrypto.h"

#include <QCoreApplication>
#include <QHostInfo>
#include <QSet>
#include <QTimer>
#include <QtConcurrent/QtConcurrent>
#include <QtGlobal>

#include <random>
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
QString wrongPinMsg() {
    return QCoreApplication::translate(
        kTrContext, "That PIN wasn't accepted. Check the code on the satellite and try again.");
}
QString pairPendingMsg() {
    return QCoreApplication::translate(
        kTrContext, "The satellite hasn't confirmed pairing yet. Try again in a moment.");
}
QString reverseDeclinedMsg() {
    return QCoreApplication::translate(
        kTrContext, "The satellite declined this device. Pairing was not approved.");
}
QString reverseTimedOutMsg() {
    return QCoreApplication::translate(
        kTrContext, "Timed out waiting for approval on the satellite. Try again.");
}

// Path-B poll cadence + budget: poll once a second for up to two minutes — the
// window an operator needs to read the PIN and approve on the satellite. The
// elapsed clock is accumulated from these intervals (not a wall-clock read) so
// the pure decision stays driven by integers the manager fully controls.
constexpr int kReversePollIntervalMs = 1000;
constexpr std::int64_t kReverseDeadlineMs = 120'000;

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
    // The pairing exchange carries the sharedKey exactly once — it gets the
    // SAME pin gate over the same store, so the first pair pins and every
    // later pairing / rotation must present the pinned cert.
    PairingClient::setPinVerifier([&pins](const QString& host, const QByteArray& certDer) {
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
        // NOTE (R6): an empty result is STATE, not a transient event. The
        // Connections page renders a distinct empty-state for
        // discoveredServers.length === 0 (separate from the scanning spinner),
        // so a redundant "No servers found" toast on top of it is removed —
        // scanning_=false with an empty discovered_ IS the "scanned, found
        // nothing" state the UI binds.
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
    QObject::connect(conn, &WifiConnection::telemetryChanged, this,
                     &WifiConnectionManager::poolTelemetryChanged);
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
    // Satellites live on the LAN by definition (mDNS/broadcast discovery); a
    // public-address literal here means a spoofed/mis-parsed beacon or a
    // poisoned remembered entry, and dialing it would leak the deviceId +
    // hmacProof to an arbitrary internet host. Refuse before any socket work.
    if (!isPrivateHostLiteral(server.ip.toStdString())) {
        emit connectionEvent(
            makeError(tr("Refusing to connect to a non-local address (%1).").arg(server.ip)));
        return;
    }
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
                    } else if constexpr (std::is_same_v<T, PairingClient::AuthRequired>) {
                        // Reachable + parsed, but no key granted: the PIN was wrong
                        // or expired — the most common, most actionable failure.
                        // Typed now (was collapsed into a generic "Pairing failed").
                        conn->markDisconnected();
                        emit connectionEvent(makeError(wrongPinMsg()));
                    } else if constexpr (std::is_same_v<T, PairingClient::Unreachable>) {
                        // Transport failure — distinct from a wrong PIN.
                        conn->markDisconnected();
                        emit connectionEvent(makeError(unreachableMsg()));
                    } else {
                        // Pending: the satellite staged the request but hasn't
                        // granted yet (rare on a direct PIN submit).
                        conn->markDisconnected();
                        emit connectionEvent(makeError(pairPendingMsg()));
                    }
                },
                outcome);
        });
    watcher->setFuture(QtConcurrent::run([server, did, dname, pin] {
        return PairingClient::pair(server.ip, server.pairPort, did, dname, pin);
    }));
}

// ── Reverse (host-initiated) pairing — Path B ───────────────────────────────

void WifiConnectionManager::requestReversePairing(const models::DiscoveredServer& server) {
    // A fresh request supersedes any in-flight one (and clears a terminal arm
    // from a previous attempt): stop the old poll before re-arming.
    cancelReversePairing();

    auto* conn = ensureConnection(server);
    retryAttempts_.remove(conn->id());
    conn->updateServer(server);

    // Generate the 4-digit clientPin. The VALUE is random; the SHAPE is fixed by
    // the pure formatter, so the displayed PIN is always exactly 4 digits. The
    // randomness lives here (the seam), never in the tested decision core.
    std::random_device rd;
    const std::uint32_t draw = rd();
    reversePin_ = QString::fromStdString(reducer::formatReversePin(draw));
    reverseServer_ = server;
    reverseServerName_ = server.name.isEmpty() ? server.ip : server.name;
    reverseElapsedMs_ = 0;
    reverseDeadlineMs_ = kReverseDeadlineMs;
    reverseSawPending_ = false;
    setReversePhase(ReversePairingPhase::AwaitingApproval);

    const QString did = deviceId_;
    const QString dname = deviceName_;
    const QString pin = reversePin_;
    // POST the clientPin (Path B). The server stages a pending grant; the reply
    // is {ok:false, pending:true} on the happy path. We then poll for approval.
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    auto* watcher = new QFutureWatcher<models::PairResponse>(this);
    QObject::connect(
        watcher, &QFutureWatcherBase::finished, this, [this, watcher, conn, server, pin] {
            const auto pair = watcher->result();
            watcher->deleteLater();
            pairingInFlight_.remove(conn->id());
            emit pairingInFlightChanged();
            // A cancel / restart that landed while this POST was in flight moved
            // us out of AwaitingApproval (or swapped the target) — drop the late
            // reply rather than starting a poll for a superseded request.
            if (reversePhase_ != ReversePairingPhase::AwaitingApproval ||
                reverseServer_.id() != server.id() || reversePin_ != pin) {
                return;
            }
            const auto outcome = PairingClient::classify(pair);
            std::visit(
                [&](auto&& arm) {
                    using T = std::decay_t<decltype(arm)>;
                    if constexpr (std::is_same_v<T, PairingClient::Success>) {
                        // Server approved synchronously (no operator step needed):
                        // adopt the key + open the session, exactly like forward.
                        conn->markConnecting();
                        store_->setSharedKey(arm.sharedKeyHex, WifiConnection::idFor(server));
                        setReversePhase(ReversePairingPhase::Approved);
                        openSession(conn, server, ConnectIntent::UserInitiated);
                    } else if constexpr (std::is_same_v<T, PairingClient::Pending>) {
                        // The expected Path-B arm: start the approval poll loop.
                        if (reverseTimer_ == nullptr) {
                            reverseTimer_ = new QTimer(this);
                            reverseTimer_->setInterval(kReversePollIntervalMs);
                            QObject::connect(reverseTimer_, &QTimer::timeout, this,
                                             &WifiConnectionManager::pollReverseStatus);
                        }
                        reverseTimer_->start();
                    } else if constexpr (std::is_same_v<T, PairingClient::VersionMismatch>) {
                        emit connectionEvent(makeError(versionMsg()));
                        finishReverse(ReversePairingPhase::Declined);
                    } else {
                        // AuthRequired / Unreachable: the POST never staged a
                        // pending grant — surface the reason and abort.
                        emit connectionEvent(makeError(pair.error.value_or(unreachableMsg())));
                        finishReverse(ReversePairingPhase::TimedOut);
                    }
                },
                outcome);
        });
    watcher->setFuture(QtConcurrent::run([server, did, dname, pin] {
        // pin=empty (no operator PIN), clientPin=the displayed PIN (Path B).
        return PairingClient::pair(server.ip, server.pairPort, did, dname, QString(), pin);
    }));
}

void WifiConnectionManager::pollReverseStatus() {
    if (reversePhase_ != ReversePairingPhase::AwaitingApproval) { return; }
    // Single-flight: a slow GET must not stack behind the 1s timer.
    if (reversePollInFlight_) { return; }
    reversePollInFlight_ = true;
    reverseElapsedMs_ += kReversePollIntervalMs;

    const QString did = deviceId_;
    const models::DiscoveredServer server = reverseServer_;
    auto* watcher = new QFutureWatcher<models::PairResponse>(this);
    QObject::connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, server] {
        const auto status = watcher->result();
        watcher->deleteLater();
        reversePollInFlight_ = false;
        // A cancel / restart raced this GET — its reply is for a superseded
        // request, so ignore it.
        if (reversePhase_ != ReversePairingPhase::AwaitingApproval ||
            reverseServer_.id() != server.id()) {
            return;
        }
        reducer::ApprovalReply ar;
        ar.status = status.httpStatus;
        ar.bodyParsed = status.reachable;
        ar.statusStr = status.status.value_or(QString()).toStdString();
        ar.hasSharedKey = status.sharedKey.has_value() && !status.sharedKey->isEmpty();
        const auto approval = reducer::classifyApproval(ar, reverseSawPending_);
        if (ar.statusStr == "pending") { reverseSawPending_ = true; }
        // The pure decision: the only place the poll loop's branching lives.
        switch (
            reducer::nextReversePairingAction(approval, reverseElapsedMs_, reverseDeadlineMs_)) {
        case reducer::ReversePairingAction::Approve: {
            // Approved with a usable key — adopt + open the session (forward path).
            auto* conn = ensureConnection(server);
            conn->markConnecting();
            store_->setSharedKey(*status.sharedKey, WifiConnection::idFor(server));
            if (reverseTimer_ != nullptr) { reverseTimer_->stop(); }
            setReversePhase(ReversePairingPhase::Approved);
            openSession(conn, server, ConnectIntent::UserInitiated);
            break;
        }
        case reducer::ReversePairingAction::Decline:
            emit connectionEvent(makeError(reverseDeclinedMsg()));
            finishReverse(ReversePairingPhase::Declined);
            break;
        case reducer::ReversePairingAction::TimeOut:
            emit connectionEvent(makeError(reverseTimedOutMsg()));
            finishReverse(ReversePairingPhase::TimedOut);
            break;
        case reducer::ReversePairingAction::KeepPolling:
            // Timer re-fires on its own; nothing to do.
            break;
        }
    });
    watcher->setFuture(QtConcurrent::run(
        [server, did] { return PairingClient::pairStatus(server.ip, server.pairPort, did); }));
}

void WifiConnectionManager::cancelReversePairing() {
    if (reverseTimer_ != nullptr) { reverseTimer_->stop(); }
    reversePollInFlight_ = false;
    if (reversePhase_ != ReversePairingPhase::Idle) {
        reversePin_.clear();
        reverseServerName_.clear();
        reverseServer_ = {};
        reverseElapsedMs_ = 0;
        setReversePhase(ReversePairingPhase::Idle);
    }
}

void WifiConnectionManager::finishReverse(ReversePairingPhase terminal) {
    if (reverseTimer_ != nullptr) { reverseTimer_->stop(); }
    reversePollInFlight_ = false;
    // Keep the pin + server name on the terminal arm so the sheet can still name
    // what it was pairing while it shows the outcome; the next request/cancel
    // clears them.
    setReversePhase(terminal);
}

void WifiConnectionManager::setReversePhase(ReversePairingPhase phase) {
    reversePhase_ = phase;
    emit reversePairingChanged();
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
    // callback can converge any slot changes that race the round-trip. NOT const:
    // a const capture is copied (not moved) when the closure moves into the
    // std::function, and that copy can throw out of the closure's move ctor.
    auto sentDescriptors = descriptorsToDesired(descriptors);

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
                },
                /*onRekey=*/
                [this, id, server] {
                    if (auto* c = connections_.value(id, nullptr)) { rekey(c, server); }
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

void WifiConnectionManager::rekey(WifiConnection* conn, const models::DiscoveredServer& server) {
    const QString id = conn->id();
    if (conn->state() != SessionState::Live) { return; }
    const auto client = conn->client();
    if (!client) { return; }
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) { return; }
    const auto pairingKey = creds->pairingKey;
    // Failures stay silent: heartbeat death / terminal-auth already surface
    // them, and a session that truly exhausts goes silent and self-heals via
    // the death-retry re-PUT.
    http_->putSession(
        server.ip, server.httpPort, deviceId_, deviceName_, creds->proof,
        conn->desiredDescriptors(), conn->wantsMouseControl(),
        [this, id, client, pairingKey](const models::SessionResponse& resp) {
            auto* c = connections_.value(id, nullptr);
            if (c == nullptr) { return; }
            using reducer::RestVerdict;
            reducer::RestReply rr;
            rr.status = resp.httpStatus;
            rr.bodyParsed = resp.reachable;
            rr.code = resp.code.value_or(QString()).toStdString();
            const RestVerdict verdict = classifyRest(rr);
            if (verdict == RestVerdict::Unauthorized) {
                onTerminalAuthFailure(c, id, ConnectIntent::RetryAfterDeath);
                return;
            }
            // A death+reconnect during the PUT flight replaced the session —
            // applying the stale material would re-arm the dead client and
            // stamp a stale epoch onto the new session.
            if (c->state() != SessionState::Live || c->client() != client) { return; }
            if (verdict != RestVerdict::Ok || !resp.token.has_value() ||
                !resp.sessionSalt.has_value()) {
                return;
            }
            const auto tok = util::fromHex(resp.token->toStdString());
            const auto salt = util::fromHex(resp.sessionSalt->toStdString());
            if (!tok || tok->size() != 4 || !salt || salt->size() != wire::kSessionSaltSize) {
                return;
            }
            std::array<std::uint8_t, 4> token{};
            std::copy_n(tok->begin(), 4, token.begin());
            std::array<std::uint8_t, wire::kSessionSaltSize> saltArr{};
            std::copy_n(salt->begin(), wire::kSessionSaltSize, saltArr.begin());
            const std::uint32_t tokenBe = (static_cast<std::uint32_t>(token[0]) << 24) |
                                          (static_cast<std::uint32_t>(token[1]) << 16) |
                                          (static_cast<std::uint32_t>(token[2]) << 8) |
                                          static_cast<std::uint32_t>(token[3]);
            std::array<std::uint8_t, 32> sessionKey{};
            wire::deriveSessionKey(pairingKey.data(), saltArr.data(), tokenBe, sessionKey.data());
            // Same socket, fresh token/key, counters restart at 1 — the hot
            // path never blips. connectionId is stable across PUTs (contract
            // §Session), so the id and slot state carry over.
            client->setConnectionParams(token, sessionKey);
            // Adopt the re-PUT's epoch so the next enriched ack doesn't read
            // as drift.
            c->adoptEpoch(resp.epoch);
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
