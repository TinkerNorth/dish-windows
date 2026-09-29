// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnectionManager.h"

#include "PairingOutcome.h"
#include "core/net/IpLiterals.h"
#include "source/http/SatelliteTlsVerifier.h"
#include "Util/Hex.h"
#include "core/reducer/Backoff.h"
#include "core/reducer/CloseNotify.h"
#include "core/reducer/HostAudioVerdict.h"
#include "core/reducer/ProtocolNegotiation.h"
#include "core/reducer/Reconcile.h"
#include "core/reducer/RestOutcome.h"
#include "core/reducer/ReversePairing.h"
#include "core/wire/SessionCrypto.h"

#include <QHostInfo>
#include <QSet>
#include <QSignalBlocker>
#include <QTimer>
#include <QtGlobal>

#include <random>
#include <type_traits>
#include <utility>
#include <variant>

namespace dish::net {

namespace {

ConnectionEvent makeError(const QString& msg) { return {ConnectionEventKind::Error, {}, msg}; }

ConnectionEvent pairingRequired(const models::DiscoveredServer& s) {
    return {ConnectionEventKind::PairingRequired, s, {}};
}

// Lives here rather than in core/reducer because it is the Qt-to-pure boundary.
std::vector<reducer::DesiredSlot>
descriptorsToDesired(const QList<models::ControllerDescriptor>& descriptors) {
    std::vector<reducer::DesiredSlot> out;
    out.reserve(static_cast<std::size_t>(descriptors.size()));
    for (const auto& d : descriptors) {
        out.push_back({static_cast<std::uint8_t>(d.ctrlIdx), d.type});
    }
    return out;
}

// What the verdict needs to know about one session PUT's reply, the connect's or the rekey's.
reducer::RestReply restReplyOf(const models::SessionResponse& resp) {
    reducer::RestReply rr;
    rr.status = resp.httpStatus;
    rr.bodyParsed = resp.reachable;
    rr.code = resp.code.value_or(QString()).toStdString();
    return rr;
}

QString unreachableMsg() {
    return WifiConnectionManager::tr(
        "Server unreachable — check it's powered on and on the same Wi-Fi.");
}
QString rePairMsg() {
    return WifiConnectionManager::tr(
        "This satellite no longer recognizes this device. Re-pair needed.");
}
QString versionMsg() {
    return WifiConnectionManager::tr(
        "This app and the satellite speak different protocol versions.");
}
// The 409 body names the satellite's range, so the message can say which end is
// behind instead of leaving the user to guess. An unusable body falls back to
// the neutral wording above rather than blaming the wrong side.
QString versionMsgFor(reducer::ProtocolVerdict verdict) {
    switch (verdict) {
    case reducer::ProtocolVerdict::UpdateDish:
        return WifiConnectionManager::tr(
            "This satellite needs a newer version of Dish. Update the app and retry.");
    case reducer::ProtocolVerdict::UpdateSatellite:
        return WifiConnectionManager::tr(
            "This satellite is too old for this version of Dish. Update the satellite.");
    case reducer::ProtocolVerdict::Settled:
    case reducer::ProtocolVerdict::RetryLower:
    case reducer::ProtocolVerdict::Unusable:
        break;
    }
    return versionMsg();
}
QString wrongPinMsg() {
    return WifiConnectionManager::tr(
        "That PIN wasn't accepted. Check the code on the satellite and try again.");
}
QString pairPendingMsg() {
    return WifiConnectionManager::tr(
        "The satellite hasn't confirmed pairing yet. Try again in a moment.");
}
QString reverseDeclinedMsg() {
    return WifiConnectionManager::tr(
        "The satellite declined this device. Pairing was not approved.");
}
QString reverseTimedOutMsg() {
    return WifiConnectionManager::tr("Timed out waiting for approval on the satellite. Try again.");
}
QString ipv6Msg(const QString& ip) {
    return WifiConnectionManager::tr("Satellite can only be reached over IPv4, and this address "
                                     "(%1) is IPv6. Scan again to find its IPv4 address.")
        .arg(ip);
}
QString wireFailedMsg() {
    return WifiConnectionManager::tr(
        "The satellite accepted, but the controller link would not open. Try again.");
}

// The window an operator needs to read the PIN and approve on the satellite. The
// elapsed clock accumulates from these intervals rather than a wall-clock read,
// so the pure decision is driven by integers the manager fully controls.
constexpr int kReversePollIntervalMs = 1000;
constexpr std::int64_t kReverseDeadlineMs = 120'000;

} // namespace

WifiConnectionManager::WifiConnectionManager(ConnectionStore* store, QObject* parent)
    : WifiConnectionManager(store, new HTTPClient, realWifiManagerEffects(), parent) {}

WifiConnectionManager::WifiConnectionManager(ConnectionStore* store, HTTPClient* http,
                                             WifiManagerEffects effects, QObject* parent)
    : QObject(parent), store_(store), http_(http), effects_(std::move(effects)) {
    http_->setParent(this);
    deviceId_ = store_->getOrCreateDeviceId();
    deviceName_ = QHostInfo::localHostName();
    if (deviceName_.isEmpty()) { deviceName_ = QStringLiteral("Windows"); }
    // TOFU on every HTTPS call, pairing included, keyed by host to match the
    // ConnectionStore pin-migration convention: the first pair pins, and every
    // later call must present the pinned cert. `pins` is captured by reference
    // below, so the store must outlive this manager.
    auto& pins = store_->facade().pins();
    http_->setPinVerifier([&pins](const QString& host, const QByteArray& certDer) {
        return http::verifyPeerCertificate(host, pins, certDer);
    });
}

WifiConnectionManager::~WifiConnectionManager() {
    // This loop exists to tear down live sessions, not to announce anything —
    // and by the time it runs there is nobody left who can safely listen.
    //
    // The manager is a QObject CHILD of AppModel, so it is deleted from
    // ~QObject's deleteChildren(), which is after every AppModel member has
    // already been destroyed — ConnectionStore among them. markDisconnected()
    // emits WifiConnection::changed, the manager relays it as poolChanged, and
    // ConnectionHub::rebuild() then reads through the store's freed
    // unique_ptr<RememberedSatelliteRepository>. That was an access violation on
    // every single exit (0xC0000005, crash.dmp written by the handler, so it
    // looked like a clean quit from outside).
    //
    // Blocking the source signal is the fix that does not depend on which
    // collaborator happens to die first. ~WifiConnection blocks for itself too,
    // which is what covers the connections this loop cannot see: forget() takes
    // one out of the map and leaves it on deleteLater, still a child of this
    // manager and still destroyed from the same deleteChildren() pass.
    for (auto* c : connections_) {
        const QSignalBlocker block(c);
        c->markDisconnected();
    }
}

void WifiConnectionManager::startDiscovery() {
    if (scanning_) { return; }
    scanning_ = true;
    emit scanningChanged();
    effects_.scan(
        this, [this](const QList<models::DiscoveredServer>& found) { onDiscoveryFinished(found); });
}

void WifiConnectionManager::onDiscoveryFinished(const QList<models::DiscoveredServer>& found) {
    discovered_ = found;
    scanning_ = false;
    // Persist a moved satellite's new IP BEFORE anything else, so the next
    // launch's autoReconnectAll and any in-flight backoff retry (which
    // re-reads store_->remembered()) target the current address. The only
    // other path that writes a fresh IP is a successful session PUT, which
    // cannot happen while the IP is wrong: that is the "must rescan, then
    // reconnect" trap.
    store_->refreshFromDiscovery(discovered_);
    // The same relearn for the in-memory connection.
    for (const auto& server : discovered_) {
        if (auto* conn = connections_.value(server.id(), nullptr)) {
            if (conn->state() == SessionState::Idle || conn->state() == SessionState::Stale) {
                conn->updateServer(server);
            }
        }
    }
    // So a moved box reconnects on its own once the scan finds it, with no
    // manual Connect.
    autoReconnectAll();
    emit discoveredChanged();
    emit scanningChanged();
    // No "found nothing" event is emitted: an empty discovered_ with
    // scanning_ false IS that state, and the page binds it directly.
}

void WifiConnectionManager::wireSlotSync(WifiConnection* conn) {
    const QString id = conn->id();
    // Converging via the per-controller routes keeps the session and UDP keys
    // from churning over a toggle.
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

// Satellites are LAN-only by definition, so a public literal here means a spoofed beacon or a
// poisoned remembered entry, and dialing it would leak the deviceId and hmacProof to an arbitrary
// internet host. A private IPv6 literal is local, but the satellite listens on IPv4 alone.
bool WifiConnectionManager::refusesHost(const models::DiscoveredServer& server,
                                        ConnectIntent intent) {
    switch (classifySatelliteHost(server.ip.toStdString())) {
    case SatelliteHostVerdict::Reachable:
        return false;
    case SatelliteHostVerdict::NotLocal:
        emit connectionEvent(
            makeError(tr("Refusing to connect to a non-local address (%1).").arg(server.ip)));
        return true;
    case SatelliteHostVerdict::NotIpv4:
        emitErrorIfUserInitiated(intent, ipv6Msg(server.ip));
        return true;
    }
    return true;
}

// A user's Disconnect holds until the user connects again: the periodic reconnect, a finished
// scan and a silent retry are all background intents, and none of them may undo it.
bool WifiConnectionManager::heldByUser(const QString& id, ConnectIntent intent) {
    if (intent == ConnectIntent::UserInitiated) {
        userDisconnected_.remove(id);
        return false;
    }
    return userDisconnected_.contains(id);
}

void WifiConnectionManager::connectTo(const models::DiscoveredServer& server,
                                      ConnectIntent intent) {
    if (refusesHost(server, intent)) { return; }
    if (heldByUser(WifiConnection::idFor(server), intent)) { return; }
    auto* conn = ensureConnection(server);
    if (intent == ConnectIntent::UserInitiated) { retryAttempts_.remove(conn->id()); }
    if (conn->state() == SessionState::Live || conn->state() == SessionState::Linking) {
        conn->updateServer(server);
        return;
    }
    conn->updateServer(server);
    conn->markConnecting();
    // With a key in hand, skip the pair handshake so a moved or offline server
    // fails fast in the session PUT instead of bouncing through PairingRequired
    // and trapping the user behind a PIN prompt.
    if (credentialsFor(conn->id()).has_value()) {
        openSession(conn, server, intent);
    } else {
        pairAndConnect(conn, server, intent);
    }
}

void WifiConnectionManager::pairWithPin(const models::DiscoveredServer& server,
                                        const QString& pin) {
    if (refusesHost(server, ConnectIntent::UserInitiated)) {
        emit pairingFailed(WifiConnection::idFor(server), QStringLiteral("unreachable"));
        return;
    }
    heldByUser(WifiConnection::idFor(server), ConnectIntent::UserInitiated);
    auto* conn = ensureConnection(server);
    retryAttempts_.remove(conn->id());
    if (conn->state() == SessionState::Live) { return; }
    conn->updateServer(server);
    conn->markConnecting();

    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    http_->pair(server.ip, server.pairPort, deviceId_, deviceName_, pin, QString(),
                [this, id = conn->id(), server](const models::PairResponse& response) {
                    onPinPairReply(id, server, PairingOutcome::classify(response));
                });
}

// Path A: the PIN the operator read off the satellite, typed into the sheet here.
void WifiConnectionManager::onPinPairReply(const QString& id,
                                           const models::DiscoveredServer& server,
                                           const PairingOutcome::Arm& outcome) {
    pairingInFlight_.remove(id);
    emit pairingInFlightChanged();
    // Forgotten while the POST was out: keeping the key would pair a satellite the user removed.
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    std::visit(
        [&](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, PairingOutcome::Success>) {
                store_->setSharedKey(arm.sharedKeyHex, id);
                openSession(conn, server, ConnectIntent::UserInitiated);
            } else if constexpr (std::is_same_v<T, PairingOutcome::VersionMismatch>) {
                conn->markDisconnected();
                emit connectionEvent(makeError(versionMsg()));
                emit pairingFailed(id, QStringLiteral("versionMismatch"));
            } else if constexpr (std::is_same_v<T, PairingOutcome::AuthRequired>) {
                // Reachable and parsed but no key granted, so the PIN was
                // wrong or expired.
                conn->markDisconnected();
                emit connectionEvent(makeError(wrongPinMsg()));
                emit pairingFailed(id, QStringLiteral("wrongPin"));
            } else if constexpr (std::is_same_v<T, PairingOutcome::Unreachable>) {
                conn->markDisconnected();
                emit connectionEvent(makeError(unreachableMsg()));
                emit pairingFailed(id, QStringLiteral("unreachable"));
            } else {
                // Pending: staged but not granted, rare on a direct submit.
                conn->markDisconnected();
                emit connectionEvent(makeError(pairPendingMsg()));
                emit pairingFailed(id, QStringLiteral("pending"));
            }
        },
        outcome);
}

// The value is random but the shape is fixed by the pure formatter, so the displayed PIN is always
// exactly 4 digits. Randomness stays out of the tested decision core.
QString WifiConnectionManager::drawReversePin() {
    std::random_device rd;
    return QString::fromStdString(reducer::formatReversePin(rd()));
}

void WifiConnectionManager::armReverseAttempt(const models::DiscoveredServer& server) {
    reversePin_ = drawReversePin();
    reverseServer_ = server;
    reverseServerName_ = server.name.isEmpty() ? server.ip : server.name;
    reverseElapsedMs_ = 0;
    reverseDeadlineMs_ = kReverseDeadlineMs;
    reverseSawPending_ = false;
    setReversePhase(ReversePairingPhase::AwaitingApproval);
}

// True while this reply still belongs to the attempt that is on screen. A cancel or a restart
// landing while the POST was in flight makes it a late reply for a superseded request, which must
// not start a poll loop of its own.
bool WifiConnectionManager::reverseAttemptIsCurrent(const models::DiscoveredServer& server,
                                                    const QString& pin) const {
    return reversePhase_ == ReversePairingPhase::AwaitingApproval &&
           reverseServer_.id() == server.id() && reversePin_ == pin;
}

// The expected arm: the operator has not answered yet, so the approval poll starts.
void WifiConnectionManager::startReversePoll() {
    if (reverseTimer_ == nullptr) {
        reverseTimer_ = new QTimer(this);
        reverseTimer_->setInterval(kReversePollIntervalMs);
        QObject::connect(reverseTimer_, &QTimer::timeout, this,
                         &WifiConnectionManager::pollReverseStatus);
    }
    reverseTimer_->start();
}

// The operator approved on the satellite, or it granted outright: key the connection and open it.
// Found or made by server rather than carried over, since a round trip lies behind either arm.
void WifiConnectionManager::adoptReverseGrant(const models::DiscoveredServer& server,
                                              const QString& sharedKeyHex) {
    auto* conn = ensureConnection(server);
    conn->markConnecting();
    store_->setSharedKey(sharedKeyHex, WifiConnection::idFor(server));
    if (reverseTimer_ != nullptr) { reverseTimer_->stop(); }
    setReversePhase(ReversePairingPhase::Approved);
    openSession(conn, server, ConnectIntent::UserInitiated);
}

void WifiConnectionManager::applyReverseOutcome(const models::DiscoveredServer& server,
                                                const models::PairResponse& pair) {
    std::visit(
        [&](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, PairingOutcome::Success>) {
                // Approved synchronously, with no operator step.
                adoptReverseGrant(server, arm.sharedKeyHex);
            } else if constexpr (std::is_same_v<T, PairingOutcome::Pending>) {
                startReversePoll();
            } else if constexpr (std::is_same_v<T, PairingOutcome::VersionMismatch>) {
                emit connectionEvent(makeError(versionMsg()));
                finishReverse(ReversePairingPhase::Declined);
            } else {
                // AuthRequired or Unreachable: no pending grant was staged.
                emit connectionEvent(makeError(pair.error.value_or(unreachableMsg())));
                finishReverse(ReversePairingPhase::TimedOut);
            }
        },
        PairingOutcome::classify(pair));
}

void WifiConnectionManager::onReversePairReply(const QString& id,
                                               const models::DiscoveredServer& server,
                                               const QString& pin,
                                               const models::PairResponse& pair) {
    pairingInFlight_.remove(id);
    emit pairingInFlightChanged();
    if (!reverseAttemptIsCurrent(server, pin)) { return; }
    applyReverseOutcome(server, pair);
}

void WifiConnectionManager::requestReversePairing(const models::DiscoveredServer& server) {
    // A fresh request supersedes any in-flight one and clears a previous attempt's terminal arm.
    cancelReversePairing();
    if (refusesHost(server, ConnectIntent::UserInitiated)) { return; }
    heldByUser(WifiConnection::idFor(server), ConnectIntent::UserInitiated);

    auto* conn = ensureConnection(server);
    retryAttempts_.remove(conn->id());
    conn->updateServer(server);
    armReverseAttempt(server);

    const QString pin = reversePin_;
    // The happy-path reply is {ok:false, pending:true}, which then gets polled.
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    // Empty operator pin, displayed pin as clientPin: that is what selects Path B server-side.
    http_->pair(server.ip, server.pairPort, deviceId_, deviceName_, QString(), pin,
                [this, id = conn->id(), server, pin](const models::PairResponse& response) {
                    onReversePairReply(id, server, pin, response);
                });
}

// What the reducer needs to know about one /pairstatus answer.
reducer::ApprovalReply WifiConnectionManager::approvalReplyOf(const models::PairResponse& status) {
    reducer::ApprovalReply ar;
    ar.status = status.httpStatus;
    ar.bodyParsed = status.reachable;
    ar.statusStr = status.status.value_or(QString()).toStdString();
    ar.hasSharedKey = status.sharedKey.has_value() && !status.sharedKey->isEmpty();
    return ar;
}

// `status` carries the shared key the Approve arm needs, which is why the reply is passed on rather
// than reduced to the action alone. The reducer only says Approve when the reply carried one, and
// value_or keeps that invariant local rather than asking a reader to carry it across two files.
void WifiConnectionManager::applyReverseAction(reducer::ReversePairingAction action,
                                               const models::PairResponse& status,
                                               const models::DiscoveredServer& server) {
    switch (action) {
    case reducer::ReversePairingAction::Approve:
        adoptReverseGrant(server, status.sharedKey.value_or(QString()));
        break;
    case reducer::ReversePairingAction::Decline:
        emit connectionEvent(makeError(reverseDeclinedMsg()));
        finishReverse(ReversePairingPhase::Declined);
        break;
    case reducer::ReversePairingAction::TimeOut:
        emit connectionEvent(makeError(reverseTimedOutMsg()));
        finishReverse(ReversePairingPhase::TimedOut);
        break;
    case reducer::ReversePairingAction::KeepPolling:
        break; // the timer re-fires on its own
    }
}

// The poll slot is free again the moment a reply lands, whether or not the reply is still wanted:
// a superseded GET that left the flag set would stall every later poll of the next attempt.
void WifiConnectionManager::onReverseStatusReply(const models::PairResponse& status,
                                                 const models::DiscoveredServer& server) {
    reversePollInFlight_ = false;
    // A cancel or restart raced this GET, so its reply is superseded. The pin is not compared here:
    // the poll carries no pin, and the phase and server together already say it is the same
    // attempt.
    if (reversePhase_ != ReversePairingPhase::AwaitingApproval ||
        reverseServer_.id() != server.id()) {
        return;
    }
    const auto reply = approvalReplyOf(status);
    const auto approval = reducer::classifyApproval(reply, reverseSawPending_);
    // Latched AFTER classifying, so the first pending answer is classified as the first one.
    if (reply.statusStr == "pending") { reverseSawPending_ = true; }
    applyReverseAction(
        reducer::nextReversePairingAction(approval, reverseElapsedMs_, reverseDeadlineMs_), status,
        server);
}

void WifiConnectionManager::pollReverseStatus() {
    if (reversePhase_ != ReversePairingPhase::AwaitingApproval) { return; }
    // A slow GET must not stack behind the 1 s timer.
    if (reversePollInFlight_) { return; }
    reversePollInFlight_ = true;
    reverseElapsedMs_ += kReversePollIntervalMs;

    const models::DiscoveredServer server = reverseServer_;
    http_->pairStatus(server.ip, server.pairPort, deviceId_,
                      [this, server](const models::PairResponse& status) {
                          onReverseStatusReply(status, server);
                      });
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
    // The pin and server name survive the terminal arm so the sheet can still
    // name what it was pairing; the next request or cancel clears them.
    setReversePhase(terminal);
}

void WifiConnectionManager::setReversePhase(ReversePairingPhase phase) {
    reversePhase_ = phase;
    emit reversePairingChanged();
}

void WifiConnectionManager::pairAndConnect(WifiConnection* conn,
                                           const models::DiscoveredServer& server,
                                           ConnectIntent intent) {
    pairingInFlight_.insert(conn->id());
    emit pairingInFlightChanged();
    http_->pair(server.ip, server.pairPort, deviceId_, deviceName_, QString(), QString(),
                [this, id = conn->id(), server, intent](const models::PairResponse& response) {
                    onConnectPairReply(id, server, intent, PairingOutcome::classify(response));
                });
}

// The pair a connect starts with when no key is on file, sent without a PIN.
void WifiConnectionManager::onConnectPairReply(const QString& id,
                                               const models::DiscoveredServer& server,
                                               ConnectIntent intent,
                                               const PairingOutcome::Arm& outcome) {
    pairingInFlight_.remove(id);
    emit pairingInFlightChanged();
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    std::visit(
        [&](auto&& arm) {
            using T = std::decay_t<decltype(arm)>;
            if constexpr (std::is_same_v<T, PairingOutcome::Success>) {
                store_->setSharedKey(arm.sharedKeyHex, id);
                openSession(conn, server, intent);
            } else if constexpr (std::is_same_v<T, PairingOutcome::AuthRequired> ||
                                 std::is_same_v<T, PairingOutcome::Pending>) {
                // First-time pair, or the server forgot us. Silent intents
                // park in Stale so the next user tap gets the dialog.
                if (intent == ConnectIntent::UserInitiated) {
                    conn->markDisconnected();
                    emit connectionEvent(pairingRequired(server));
                } else {
                    conn->markStale();
                }
            } else if constexpr (std::is_same_v<T, PairingOutcome::VersionMismatch>) {
                conn->markDisconnected();
                emitErrorIfUserInitiated(intent, versionMsg());
            } else {
                if (intent == ConnectIntent::UserInitiated) {
                    conn->markDisconnected();
                    emit connectionEvent(makeError(unreachableMsg()));
                } else {
                    conn->markStale();
                }
            }
        },
        outcome);
}

void WifiConnectionManager::openSession(WifiConnection* conn,
                                        const models::DiscoveredServer& server,
                                        ConnectIntent intent) {
    const QString id = conn->id();
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) {
        onTerminalAuthFailure(conn, id, intent);
        return;
    }
    const auto descriptors = conn->desiredDescriptors();
    // Lets the reply converge slot changes that raced the round-trip.
    // NOT const: a const capture is copied rather than moved into the
    // std::function, and that copy can throw out of the closure's move ctor.
    auto sentDescriptors = descriptorsToDesired(descriptors);

    http_->putSession(server.ip, server.httpPort, deviceId_, deviceName_, creds->proof, descriptors,
                      conn->wantsMouseControl(), conn->offeredProtocolVersion(),
                      [this, id, server, intent, sent = *creds,
                       sentDescriptors](const models::SessionResponse& resp) {
                          onSessionReply(id, server, intent, sent, sentDescriptors, resp);
                      });
}

void WifiConnectionManager::onSessionReply(const QString& id,
                                           const models::DiscoveredServer& server,
                                           ConnectIntent intent, const Credentials& sent,
                                           const std::vector<reducer::DesiredSlot>& sentDescriptors,
                                           const models::SessionResponse& resp) {
    // Forgotten while the PUT was out: a session started now would bring back what the user
    // removed.
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto verdict = reducer::classifyRest(restReplyOf(resp));
    if (verdict != reducer::RestVerdict::Ok || !resp.connectionId || !resp.token ||
        !resp.sessionSalt) {
        onSessionRefused(conn, server, intent, verdict, resp);
        return;
    }
    const auto material = sessionMaterialFrom(resp, sent.pairingKey);
    if (!material.has_value()) {
        releaseUnusableGrant(conn, server, *resp.connectionId, sent.proof, intent);
        return;
    }
    const auto client = effects_.openLink(server.ip.toStdString(), server.udpPort);
    if (!client) {
        releaseUnusableGrant(conn, server, *resp.connectionId, sent.proof, intent);
        return;
    }
    startSession(conn, server, client, *resp.connectionId, resp, *material);
    convergeLateSlots(conn, sentDescriptors);
}

// Each refusal has its own way out. An Ok arrives here only when it is missing part of the session.
void WifiConnectionManager::onSessionRefused(WifiConnection* conn,
                                             const models::DiscoveredServer& server,
                                             ConnectIntent intent, reducer::RestVerdict verdict,
                                             const models::SessionResponse& resp) {
    switch (verdict) {
    case reducer::RestVerdict::Unauthorized:
        onTerminalAuthFailure(conn, conn->id(), intent);
        return;
    case reducer::RestVerdict::VersionMismatch:
        onVersionRejected(conn, server, intent, resp);
        return;
    case reducer::RestVerdict::Ok:
    case reducer::RestVerdict::ShuttingDown:
    case reducer::RestVerdict::Unreachable:
    case reducer::RestVerdict::ServerError:
        // Unreachable, 503 or malformed: park and back off.
        if (intent == ConnectIntent::UserInitiated) {
            conn->markDisconnected();
            emit connectionEvent(makeError(unreachableMsg()));
        } else {
            conn->markStale();
        }
        scheduleRetry(server, intent);
        return;
    }
}

void WifiConnectionManager::startSession(WifiConnection* conn,
                                         const models::DiscoveredServer& server,
                                         const std::shared_ptr<SatelliteClient>& client,
                                         const QString& connectionId,
                                         const models::SessionResponse& resp,
                                         const SessionMaterial& material) {
    const QString id = conn->id();
    // The SETTLED version, not the offered one: a pre-versioning
    // satellite echoes 1 whatever we asked for, and the 0x000C frame
    // shape follows the echo.
    const auto negotiated = reducer::settleAccepted(resp.protocolVersion);
    conn->setSettledProtocolVersion(negotiated.settledVersion, negotiated.satelliteBehind);
    conn->setProtocolCompat(reducer::compatForOutcome(negotiated));
    client->setConnectionParams(material.token, material.sessionKey, negotiated.settledVersion);
    store_->remember(server);
    retryAttempts_.remove(id);

    conn->markConnected(
        client, connectionId, resp.epoch, resp.mouseControl.granted,
        /*onDead=*/
        [this, id, server] {
            closeSession(id);
            scheduleRetry(server, ConnectIntent::RetryAfterDeath);
        },
        /*onClose=*/
        [this, id, server](std::uint8_t reason) {
            if (auto* c = connections_.value(id, nullptr)) { handleServerClose(c, server, reason); }
        },
        /*onReconcile=*/
        [this, id, server] {
            if (auto* c = connections_.value(id, nullptr)) { reconcile(c, server); }
        },
        /*onRekey=*/
        [this, id, server] {
            if (auto* c = connections_.value(id, nullptr)) { rekey(c, server); }
        });
    conn->applyResults(resp.controllers);
    probeHostAudio(id, server);
}

// The reply applied what was SENT, so a slot the user changed during the round trip is removed or
// re-sent on its own route.
void WifiConnectionManager::convergeLateSlots(WifiConnection* conn,
                                              const std::vector<reducer::DesiredSlot>& sent) {
    const QString id = conn->id();
    const auto converge =
        reducer::lateSlotConverge(sent, descriptorsToDesired(conn->desiredDescriptors()));
    for (std::uint8_t ctrlIdx : converge.removes) { deleteSlot(id, ctrlIdx); }
    for (std::uint8_t ctrlIdx : converge.resyncs) {
        const QString slotId = conn->slotIdForIndex(ctrlIdx);
        if (!slotId.isEmpty()) { syncSlot(id, slotId); }
    }
}

void WifiConnectionManager::onVersionRejected(WifiConnection* conn,
                                              const models::DiscoveredServer& server,
                                              ConnectIntent intent,
                                              const models::SessionResponse& resp) {
    // A user's Disconnect landed while the 409 was in flight and already ended this attempt.
    if (conn->state() != SessionState::Linking) { return; }
    const auto negotiated =
        reducer::settleRejected(resp.supportedProtocol, resp.supportedProtocolMin);
    const bool rangesOverlap = negotiated.verdict == reducer::ProtocolVerdict::RetryLower;
    // Strictly lower, so a satellite that rejects its own ceiling ends the attempt, not loops.
    const bool offersSomethingNew = negotiated.settledVersion < conn->offeredProtocolVersion();
    if (rangesOverlap && offersSomethingNew) {
        // Re-offered at once, for every intent, as dish-android does: the answer is already
        // known, so a backoff would only delay it. The lowered offer sticks to this connection.
        conn->setOfferedProtocolVersion(negotiated.settledVersion);
        openSession(conn, server, intent);
        return;
    }
    // The row keeps saying which end must update after the attempt is torn down; an unreadable
    // 409 leaves the chip alone.
    conn->setProtocolCompat(reducer::compatForOutcome(negotiated));
    conn->markDisconnected();
    emitErrorIfUserInitiated(intent, versionMsgFor(negotiated.verdict));
}

void WifiConnectionManager::reconcile(WifiConnection* conn,
                                      const models::DiscoveredServer& server) {
    const QString id = conn->id();
    if (conn->state() != SessionState::Live) { return; }
    const auto connId = conn->connectionId();
    if (!connId.has_value()) { return; }
    auto client = conn->client();
    if (!client) { return; }
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
                              // Benign drift, e.g. our own standalone PUT raced an ack.
                              c->adoptEpoch(view.epoch);
                              return;
                          }
                          // Tear the UDP tuple down first, since the converging PUT
                          // rotates the token and key.
                          c->markDisconnected();
                          c->markConnecting();
                          openSession(c, server, ConnectIntent::RetryAfterDeath);
                      });
}

// The token and salt a session PUT must both carry, in the sizes the wire fixes. Null for a reply
// that is missing either, or carries one at the wrong length: there is nothing to adopt.
std::optional<WifiConnectionManager::SessionMaterial>
WifiConnectionManager::sessionMaterialFrom(const models::SessionResponse& resp,
                                           const std::array<std::uint8_t, 32>& pairingKey) {
    if (!resp.token.has_value() || !resp.sessionSalt.has_value()) { return std::nullopt; }
    const auto tok = util::fromHex(resp.token->toStdString());
    const auto salt = util::fromHex(resp.sessionSalt->toStdString());
    if (!tok || tok->size() != 4 || !salt || salt->size() != wire::kSessionSaltSize) {
        return std::nullopt;
    }
    SessionMaterial m;
    std::copy_n(tok->begin(), 4, m.token.begin());
    const std::uint32_t tokenBe = (static_cast<std::uint32_t>(m.token[0]) << 24) |
                                  (static_cast<std::uint32_t>(m.token[1]) << 16) |
                                  (static_cast<std::uint32_t>(m.token[2]) << 8) |
                                  static_cast<std::uint32_t>(m.token[3]);
    wire::deriveSessionKey(pairingKey.data(), salt->data(), tokenBe, m.sessionKey.data());
    return m;
}

// Same socket, fresh token and key, counters back to 1, so the hot path never blips.
// connectionId is stable across PUTs, so the id and slot state carry over.
void WifiConnectionManager::adoptRekey(WifiConnection* c, const QString& id,
                                       const std::shared_ptr<SatelliteClient>& client,
                                       const models::SessionResponse& resp,
                                       const SessionMaterial& material) {
    // A re-PUT settles again: the satellite could have been upgraded under a live session, and
    // the frame shape must follow the answer it just gave, not the one it gave at connect.
    const auto negotiated = reducer::settleAccepted(resp.protocolVersion);
    c->setSettledProtocolVersion(negotiated.settledVersion, negotiated.satelliteBehind);
    c->setProtocolCompat(reducer::compatForOutcome(negotiated));
    client->setConnectionParams(material.token, material.sessionKey, negotiated.settledVersion);
    // Otherwise the next enriched ack would read as drift.
    c->adoptEpoch(resp.epoch);
    // The satellite could have been upgraded or re-switched under the live session, same reason
    // the protocol version re-settles above.
    probeHostAudio(id, c->server());
}

// Failures stay silent: heartbeat death and terminal-auth already surface them.
void WifiConnectionManager::onRekeyReply(const QString& id,
                                         const std::shared_ptr<SatelliteClient>& client,
                                         const std::array<std::uint8_t, 32>& pairingKey,
                                         const models::SessionResponse& resp) {
    auto* c = connections_.value(id, nullptr);
    if (c == nullptr) { return; }

    using reducer::RestVerdict;
    const RestVerdict verdict = reducer::classifyRest(restReplyOf(resp));
    if (verdict == RestVerdict::Unauthorized) {
        onTerminalAuthFailure(c, id, ConnectIntent::RetryAfterDeath);
        return;
    }
    // If a death and reconnect replaced the session mid-flight, applying this material would
    // re-arm the dead client and stamp a stale epoch onto the new session.
    if (c->state() != SessionState::Live || c->client() != client) { return; }
    if (verdict != RestVerdict::Ok) { return; }

    // Nothing to adopt; the death retry is what heals a session that truly exhausts.
    const auto material = sessionMaterialFrom(resp, pairingKey);
    if (!material.has_value()) { return; }
    adoptRekey(c, id, client, resp, *material);
}

void WifiConnectionManager::rekey(WifiConnection* conn, const models::DiscoveredServer& server) {
    if (conn->state() != SessionState::Live) { return; }
    const auto client = conn->client();
    if (!client) { return; }
    const QString id = conn->id();
    const auto creds = credentialsFor(id);
    if (!creds.has_value()) { return; }

    const auto pairingKey = creds->pairingKey;
    http_->putSession(server.ip, server.httpPort, deviceId_, deviceName_, creds->proof,
                      conn->desiredDescriptors(), conn->wantsMouseControl(),
                      conn->offeredProtocolVersion(),
                      [this, id, client, pairingKey](const models::SessionResponse& resp) {
                          onRekeyReply(id, client, pairingKey, resp);
                      });
}

void WifiConnectionManager::probeHostAudio(const QString& id,
                                           const models::DiscoveredServer& server) {
    // Unauthenticated read; a failure keeps the conservative "no audio"
    // default rather than surfacing anything, because the absence of a verdict
    // and a verdict of no are deliberately the same state.
    http_->getCapabilities(
        server.ip, server.httpPort, [this, id](const models::CapabilitiesDto& caps) {
            auto* c = connections_.value(id, nullptr);
            if (c == nullptr || c->state() != SessionState::Live) { return; }
            if (!caps.reachable || caps.httpStatus < 200 || caps.httpStatus > 299) { return; }
            const auto verdict = reducer::resolveHostControllerAudio(caps);
            c->setHostControllerAudio(verdict.mic, verdict.speaker, verdict.hapticAudio);
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
                                 // 404: the session died under us, and the alive-poll
                                 // and close-notify paths own recovery.
                                 return;
                             }
                             c->adoptEpoch(resp.epoch);
                             c->applyResults(QList<models::ControllerApplyDto>{*resp.controller});
                             // The grant is only computed at session PUT, so a
                             // mouse-mode toggle needs the whole session converged.
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
        // unpaired: trust revoked.
        conn->markStale();
        store_->forgetKey(id);
        break;
    case reducer::CloseAction::StayDown:
        // replaced: a newer PUT already owns the session.
        conn->markDisconnected();
        break;
    case reducer::CloseAction::RetryBackoff:
        // shutdown or kicked, both transient.
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
    effects_.after(static_cast<int>(delay), retryScopeFor(id),
                   [this, id, server] { onRetryDue(id, server); });
}

void WifiConnectionManager::onRetryDue(const QString& id, const models::DiscoveredServer& server) {
    auto* c = connections_.value(id, nullptr);
    if (c == nullptr) { return; }
    // Retry only from a settled state: a user-driven reconnect or forget in
    // the interim moved it out, and clobbering that would fight the user.
    if (c->state() != SessionState::Idle && c->state() != SessionState::Stale) { return; }
    // Idempotent, and on completion it persists any new IP and re-runs
    // autoReconnectAll, so a box that moved DHCP leases reconnects on its own
    // without the user opening Manage and pressing Scan.
    startDiscovery();
    // The direct attempt below still runs, so a satellite discovery cannot
    // reach (mDNS and broadcast blocked on the segment) is not left waiting on
    // a scan that may find nothing.
    models::DiscoveredServer target = server;
    for (const auto& r : store_->remembered()) {
        if (r.id == id) {
            target = r.toDiscovered();
            break;
        }
    }
    connectTo(target, ConnectIntent::RetryAfterDeath);
}

QObject* WifiConnectionManager::retryScopeFor(const QString& id) {
    auto* scope = retryScopes_.value(id, nullptr);
    if (scope == nullptr) {
        scope = new QObject(this);
        retryScopes_.insert(id, scope);
    }
    return scope;
}

// A retry is armed with its satellite's scope as the timer's context, and Qt drops a single-shot
// whose context is gone, so deleting the scope cancels every retry still waiting under it.
void WifiConnectionManager::cancelPendingRetries(const QString& id) {
    delete retryScopes_.take(id);
}

void WifiConnectionManager::onTerminalAuthFailure(WifiConnection* conn, const QString& id,
                                                  ConnectIntent intent) {
    // 401 NOT_PAIRED / BAD_PROOF, or no usable key at all. Terminal by contract,
    // so the retry curve stops here rather than hammering a revoked device.
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
    userDisconnected_.insert(id);
    closeSession(id);
}

void WifiConnectionManager::closeSession(const QString& id) {
    // First, since a silent retry that fired afterwards would dial the satellite straight back.
    // A death's own retry is scheduled after this call returns, so it survives.
    cancelPendingRetries(id);
    auto* conn = connections_.value(id, nullptr);
    if (conn == nullptr) { return; }
    const auto server = conn->server();
    const auto cid = conn->connectionId();
    const auto creds = credentialsFor(id);
    conn->markDisconnected();
    if (cid.has_value() && creds.has_value()) { releaseSession(server, *cid, creds->proof); }
}

void WifiConnectionManager::releaseSession(const models::DiscoveredServer& server,
                                           const QString& connectionId, const QString& proof) {
    http_->deleteSession(server.ip, server.httpPort, connectionId, deviceId_, proof,
                         [](int, bool, const QString&) {});
}

void WifiConnectionManager::releaseUnusableGrant(WifiConnection* conn,
                                                 const models::DiscoveredServer& server,
                                                 const QString& connectionId, const QString& proof,
                                                 ConnectIntent intent) {
    conn->markDisconnected();
    releaseSession(server, connectionId, proof);
    emitErrorIfUserInitiated(intent, wireFailedMsg());
}

void WifiConnectionManager::forget(const QString& id) {
    auto* conn = connections_.value(id, nullptr);
    // Self-unpair BEFORE dropping the key, since the proof needs it. Otherwise a
    // forgotten dish leaves a paired ghost row on the satellite.
    if (conn != nullptr) {
        const auto server = conn->server();
        const auto creds = credentialsFor(id);
        if (creds.has_value()) {
            http_->unpair(server.ip, server.httpPort, deviceId_, creds->proof,
                          [](int, bool, const QString&) {});
        }
    }
    closeSession(id);
    store_->forget(id);
    retryAttempts_.remove(id);
    userDisconnected_.remove(id);
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

void WifiConnectionManager::prepareForSleep() {
    // Snapshot the keys: closeSession() fans out through poolChanged into the
    // hub's rebuild, which reshapes connections_ under a live iterator. Not
    // disconnect(): a sleep is not the user asking a satellite to stay down.
    const auto ids = connections_.keys();
    for (const auto& id : ids) { closeSession(id); }
}

void WifiConnectionManager::resumeFromSleep() {
    // A backoff curve armed before the suspend is measuring wall clock the
    // machine spent asleep, so the first attempt after a resume starts over.
    retryAttempts_.clear();
    // Rescan before reconnecting: a laptop that resumes on another network has
    // a stale remembered IP, and only discovery can relearn it.
    startDiscovery();
    autoReconnectAll();
}

} // namespace dish::net
