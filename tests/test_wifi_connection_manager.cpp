// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Drives the real WifiConnectionManager against a satellite whose REST API is
// answered in-process (FakeSatelliteRest.h), on virtual time with no scan and
// no socket (FakeWifiEffects.h). Every REST call it makes is seen, and nothing
// leaves the process.

#include "Network/ConnectionStore.h"
#include "Network/HTTPClient.h"
#include "Network/WifiConnection.h"
#include "Network/WifiConnectionManager.h"
#include "core/model/Protocol.h"
#include "core/net/Tofu.h"
#include "core/wire/SessionCrypto.h"

#include "FakeHttpsListener.h"
#include "FakeSatelliteRest.h"
#include "FakeWifiEffects.h"
#include "InstalledCatalog.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEvent>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QSettings>
#include <QString>

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using dish::models::DiscoveredServer;
using dish::net::ConnectIntent;
using dish::net::ConnectionEvent;
using dish::net::ConnectionEventKind;
using dish::net::ReversePairingPhase;
using dish::net::SessionState;
using dish::test::CannedAnswer;
using dish::test::FakeSatelliteRest;
using dish::test::FakeWifiEffects;
using dish::test::InstalledCatalog;

namespace {

constexpr int kHttpPort = 9443;
constexpr int kUdpPort = 9876;
constexpr std::uint8_t kPairingKeyByte = 0x11;
constexpr std::size_t kPairingKeySize = 32;
// Bounds the event-loop turns a canned REST reply needs; nothing waits on a clock.
constexpr int kReplyTimeoutMs = 5000;
// Past the first silent retry's 1 s backoff (reducer::backoffDelayMs(1)).
constexpr int kPastFirstBackoffMs = 1100;
// The version a satellite one protocol behind this build answers a 409 with.
constexpr int kOneBehind = dish::proto::kProtocolVersion - 1;

const QString kSessionsPath = QStringLiteral("/api/connections");
const QString kPairPath = QStringLiteral("/api/pair");
const QString kStatusPath = QStringLiteral("/api/pair/status");
const QString kGrantedSessionPath = QStringLiteral("/api/connections/conn_1");
const QString kPrivateIp = QStringLiteral("10.0.0.5");
const QString kOtherPrivateIp = QStringLiteral("10.0.0.6");
const QString kIpv6Ip = QStringLiteral("[fd00::5]");
const char* const kManagerContext = "dish::net::WifiConnectionManager";

const QString kWireFailedMsg =
    QStringLiteral("The satellite accepted, but the controller link would not open. Try again.");
const QString kIpv6Msg = QStringLiteral("Satellite can only be reached over IPv4, and this address "
                                        "(%1) is IPv6. Scan again to find its IPv4 address.");
const QString kUnreachableMsg =
    QStringLiteral("Server unreachable — check it's powered on and on the same Wi-Fi.");
const QString kVersionMsg =
    QStringLiteral("This app and the satellite speak different protocol versions.");
const QString kIdentityChangedMsg =
    QStringLiteral("This satellite's security identity changed. If it was reinstalled, forget it "
                   "here and pair again.");

const QByteArray kGrant = R"({"connectionId":"conn_1","token":"00000001",)"
                          R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortToken = R"({"connectionId":"conn_1","token":"0001",)"
                                        R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortSalt = R"({"connectionId":"conn_1","token":"00000001",)"
                                       R"("sessionSalt":"0102","epoch":1})";
const QByteArray kShuttingDown = R"({"code":"SHUTTING_DOWN"})";
// A pair the satellite grants outright, with a key in the shape the store accepts.
const QByteArray kPairGranted = R"({"ok":true,"sharedKey":")" +
                                QByteArray(static_cast<qsizetype>(kPairingKeySize * 2), '2') +
                                R"("})";

QByteArray versionConflict(int supported) {
    return QStringLiteral(R"({"supported":%1,"supportedMin":1})").arg(supported).toUtf8();
}

DiscoveredServer satelliteAt(const QString& ip, const QString& machineId) {
    DiscoveredServer s;
    s.machineId = machineId;
    s.ip = ip;
    s.name = QStringLiteral("Pc");
    s.udpPort = kUdpPort;
    s.pairPort = kHttpPort;
    s.httpPort = kHttpPort;
    return s;
}

// Canned replies land on a later event-loop turn; this runs those turns.
bool pumpUntil(const std::function<bool()>& done) {
    const QDeadlineTimer deadline(kReplyTimeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) { return false; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

// What the manager's own verifier answers for a satellite whose certificate is not the one pinned
// for its address: it flags the change and refuses the handshake.
bool refusesAsChanged(const QString&, const QByteArray&, bool& pinMismatch) {
    pinMismatch = true;
    return false;
}

// The same satellite's own certificate, back again.
bool acceptsTheCertificate(const QString&, const QByteArray&, bool&) { return true; }

int offeredVersionOf(const dish::test::RecordedRequest& put) {
    return QJsonDocument::fromJson(put.body)
        .object()
        .value(QStringLiteral("protocolVersion"))
        .toInt();
}

// One paired satellite, a second one for the cases that need a bystander, a
// manager wired to the in-process REST API and virtual time, and every event
// the manager raised.
struct ManagerFixture {
    DiscoveredServer server;
    DiscoveredServer other;
    QString id;
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<dish::net::ConnectionStore> store;
    FakeWifiEffects effects;
    FakeSatelliteRest* rest = nullptr;     // owned by the manager's HTTPClient
    dish::net::HTTPClient* http = nullptr; // owned by the manager
    std::unique_ptr<dish::net::WifiConnectionManager> manager;
    std::vector<ConnectionEvent> events;
    std::vector<QString> pairingFailures;

    explicit ManagerFixture(const QString& ip = kPrivateIp)
        : server(satelliteAt(ip, QStringLiteral("m1"))),
          other(satelliteAt(kOtherPrivateIp, QStringLiteral("m2"))),
          id(dish::net::WifiConnection::idFor(server)), settings(dish::test::makeSharedSettings()) {
        dish::test::ensureApp();
        store = std::make_unique<dish::net::ConnectionStore>(
            std::make_unique<QSettings>(settings->fileName(), QSettings::IniFormat));
        pair(server);
        pair(other);
        rest = new FakeSatelliteRest;
        http = new dish::net::HTTPClient(rest, nullptr);
        manager = std::make_unique<dish::net::WifiConnectionManager>(store.get(), http,
                                                                     effects.effects(), nullptr);
        QObject::connect(manager.get(), &dish::net::WifiConnectionManager::connectionEvent,
                         manager.get(), [this](const ConnectionEvent& e) { events.push_back(e); });
        QObject::connect(
            manager.get(), &dish::net::WifiConnectionManager::pairingFailed, manager.get(),
            [this](const QString&, const QString& reason) { pairingFailures.push_back(reason); });
    }

    void pair(const DiscoveredServer& s) {
        const QString key(static_cast<int>(kPairingKeySize) * 2, QLatin1Char('1'));
        store->setSharedKey(key, dish::net::WifiConnection::idFor(s));
    }

    SessionState state() const { return manager->get(id)->state(); }

    bool settles() {
        return pumpUntil([this] { return state() != SessionState::Linking; });
    }

    long long sessionPutsTo(const QString& ip) const {
        long long puts = 0;
        for (const auto& r : rest->requests()) {
            const bool isSessionPut = r.verb == "PUT" && r.path == kSessionsPath;
            if (isSessionPut && r.host == ip) { ++puts; }
        }
        return puts;
    }

    std::vector<dish::test::RecordedRequest> sessionPuts() const {
        std::vector<dish::test::RecordedRequest> puts;
        for (const auto& r : rest->requests()) {
            if (r.verb == "PUT" && r.path == kSessionsPath) { puts.push_back(r); }
        }
        return puts;
    }

    // The satellite answers from here on with a certificate other than the one pinned for it, as a
    // reinstalled machine at a remembered address does.
    void presentChangedCertificate() {
        rest->handshakeEveryReply();
        http->setPinVerifier(&refusesAsChanged);
    }

    bool remembers(const DiscoveredServer& s) const {
        const QString sid = dish::net::WifiConnection::idFor(s);
        for (const auto& r : store->remembered()) {
            if (r.id == sid) { return true; }
        }
        return false;
    }

    // Forgets the satellite while its request waits on the satellite, and lets the reply land
    // before the deferred delete forget() asked for has run: a reply already queued when the user
    // pressed Forget. The connection is gone by the time this returns.
    void forgetThenReleaseBeforeTheDelete() {
        const QPointer<dish::net::WifiConnection> conn = manager->get(id);
        REQUIRE(conn);
        manager->forget(id);
        releaseAndDeliver();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        REQUIRE(conn.isNull());
    }

    // The same, with the reply landing once the forgotten connection has been deleted.
    void forgetThenReleaseAfterTheDelete() {
        const QPointer<dish::net::WifiConnection> conn = manager->get(id);
        REQUIRE(conn);
        manager->forget(id);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        REQUIRE(conn.isNull());
        releaseAndDeliver();
    }

    void releaseAndDeliver() {
        rest->release();
        REQUIRE(pumpUntil([this] { return rest->allAnswered(); }));
    }

    std::string expectedProof() const {
        std::array<std::uint8_t, kPairingKeySize> key{};
        key.fill(kPairingKeyByte);
        const std::string deviceId = store->getOrCreateDeviceId().toStdString();
        return dish::wire::computeHmacProof(key.data(), deviceId);
    }

    // A silent connect that the satellite turns away with a 503, which parks the
    // row Stale and arms the first backoff retry.
    void failSilently() {
        rest->answerOnce("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
        manager->connectTo(server, ConnectIntent::AutoReconnect);
        REQUIRE(settles());
        REQUIRE(state() == SessionState::Stale);
    }
};

// The satellite granted a session (it now holds a slot for conn_1) and the
// link never came up here.
void requireGrantReleased(const ManagerFixture& f) {
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
    REQUIRE(f.rest->count("DELETE", kGrantedSessionPath) == 1);
    const auto& release = f.rest->requests().back();
    REQUIRE(release.path == kGrantedSessionPath);
    REQUIRE(release.deviceId.toStdString() == f.store->getOrCreateDeviceId().toStdString());
    REQUIRE(release.hmacProof.toStdString() == f.expectedProof());
}

void requireOneError(const ManagerFixture& f, const QString& message) {
    REQUIRE(f.events.size() == 1);
    REQUIRE(f.events.front().kind == ConnectionEventKind::Error);
    REQUIRE(f.events.front().message.toStdString() == message.toStdString());
}

void requireNoRetry(ManagerFixture& f) {
    REQUIRE(f.effects.pendingCalls() == 0);
    f.effects.advance(kPastFirstBackoffMs);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
}

} // namespace

// ---- A granted session the link cannot use ----

TEST_CASE("wifi manager: a user connect whose socket will not open reports it and releases the "
          "granted session",
          "[manager][grant]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.effects.linkAttempts() == 1);
    requireGrantReleased(f);
    requireOneError(f, kWireFailedMsg);
}

TEST_CASE("wifi manager: a silent connect whose socket will not open releases the granted session "
          "quietly and does not retry",
          "[manager][grant]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    REQUIRE(f.events.empty());
    requireNoRetry(f);
}

TEST_CASE("wifi manager: a granted token of the wrong length is reported and the session released",
          "[manager][grant]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortToken});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.effects.linkAttempts() == 0);
    requireGrantReleased(f);
    requireOneError(f, kWireFailedMsg);
}

TEST_CASE("wifi manager: a granted salt of the wrong length is reported and the session released",
          "[manager][grant]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortSalt});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.effects.linkAttempts() == 0);
    requireGrantReleased(f);
    requireOneError(f, kWireFailedMsg);
}

TEST_CASE("wifi manager: a silent connect with unusable material releases the session quietly",
          "[manager][grant]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortToken});

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    REQUIRE(f.events.empty());
    requireNoRetry(f);
}

// ---- A reply that lands after a forget ----

TEST_CASE("wifi manager: a session granted after a forget does not reach the forgotten satellite",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});
    f.rest->hold();

    // A key on file, so the connect goes straight to the session PUT.
    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    SECTION("landing before the forgotten connection is deleted") {
        f.forgetThenReleaseBeforeTheDelete();
    }
    SECTION("landing after the forgotten connection is deleted") {
        f.forgetThenReleaseAfterTheDelete();
    }

    REQUIRE(f.effects.linkAttempts() == 0);
    REQUIRE(f.manager->get(f.id) == nullptr);
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE_FALSE(f.remembers(f.server));
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: a PIN pair's key that lands after a forget is not kept",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answer("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});
    f.rest->hold();

    f.manager->pairWithPin(f.server, QStringLiteral("1234"));
    SECTION("landing before the forgotten connection is deleted") {
        f.forgetThenReleaseBeforeTheDelete();
    }
    SECTION("landing after the forgotten connection is deleted") {
        f.forgetThenReleaseAfterTheDelete();
    }

    REQUIRE_FALSE(f.manager->isPairingInFlight(f.id));
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.manager->get(f.id) == nullptr);
    // Keyed, it would have gone straight on to open a session.
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE(f.pairingFailures.empty());
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: a connect's pair that lands after a forget does not key the satellite "
          "again",
          "[manager][forget]") {
    ManagerFixture f;
    // No key on file, so the connect starts with a PIN-less pair.
    f.store->forgetKey(f.id);
    f.rest->answer("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});
    f.rest->hold();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    SECTION("landing before the forgotten connection is deleted") {
        f.forgetThenReleaseBeforeTheDelete();
    }
    SECTION("landing after the forgotten connection is deleted") {
        f.forgetThenReleaseAfterTheDelete();
    }

    REQUIRE_FALSE(f.manager->isPairingInFlight(f.id));
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.manager->get(f.id) == nullptr);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: an approval request's reply that lands after the sheet closed and a "
          "Forget is dropped",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answer("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();

    f.manager->requestReversePairing(f.server);
    // Closing the sheet cancels the attempt, and only then can the user reach Forget.
    f.manager->cancelReversePairing();
    f.forgetThenReleaseAfterTheDelete();

    REQUIRE_FALSE(f.manager->isPairingInFlight(f.id));
    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::Idle);
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.manager->get(f.id) == nullptr);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE_FALSE(f.remembers(f.server));
}

TEST_CASE("wifi manager: forgetting a satellite ends the approval request aimed at it",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answer("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();

    f.manager->requestReversePairing(f.server);
    f.forgetThenReleaseAfterTheDelete();

    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::Idle);
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.manager->get(f.id) == nullptr);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE_FALSE(f.remembers(f.server));
}

TEST_CASE("wifi manager: forgetting a satellite drops the pin its address was given, remembered or "
          "not",
          "[manager][forget][identity]") {
    ManagerFixture f;
    // Pinned by an approval request's first handshake, for a satellite that never paired.
    f.store->facade().pins().pin(f.server.ip, QString(64, QLatin1Char('0')));
    f.manager->requestReversePairing(f.server);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));
    REQUIRE_FALSE(f.remembers(f.server));

    f.manager->forget(f.id);

    REQUIRE_FALSE(f.store->facade().pins().pinnedFingerprint(f.server.ip).has_value());
}

// ---- A reply from before a Forget, landing on the connection a fresh pair made ----

TEST_CASE(
    "wifi manager: a session PUT sent before a Forget leaves the connection a fresh pair made "
    "alone",
    "[manager][forget]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.rest->hold();
    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    f.manager->forget(f.id);
    f.manager->pairWithPin(f.server, QStringLiteral("1234"));

    f.rest->releaseFirst("PUT", kSessionsPath);
    REQUIRE(pumpUntil([&f] { return f.rest->delivered() == 1; }));

    REQUIRE(f.state() == SessionState::Linking);
    REQUIRE(f.manager->isPairingInFlight(f.id));
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: a PIN pair's key from before a Forget does not key the connection a "
          "fresh pair made",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answerOnce("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();
    f.manager->pairWithPin(f.server, QStringLiteral("1111"));
    f.manager->forget(f.id);
    f.manager->pairWithPin(f.server, QStringLiteral("2222"));

    f.rest->releaseFirst("POST", kPairPath);
    REQUIRE(pumpUntil([&f] { return f.rest->delivered() == 1; }));

    // The fresh pair is still out, and the old answer neither ended it nor keyed it.
    REQUIRE(f.manager->isPairingInFlight(f.id));
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE(f.state() == SessionState::Linking);
}

TEST_CASE("wifi manager: a connect's pair from before a Forget does not key the connection a "
          "fresh pair made",
          "[manager][forget]") {
    ManagerFixture f;
    // No key on file, so the connect starts with a PIN-less pair.
    f.store->forgetKey(f.id);
    f.rest->answerOnce("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();
    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    f.manager->forget(f.id);
    f.manager->pairWithPin(f.server, QStringLiteral("2222"));

    f.rest->releaseFirst("POST", kPairPath);
    REQUIRE(pumpUntil([&f] { return f.rest->delivered() == 1; }));

    REQUIRE(f.manager->isPairingInFlight(f.id));
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 0);
    REQUIRE(f.state() == SessionState::Linking);
}

TEST_CASE("wifi manager: an approval request's reply from before a Forget leaves a fresh pair in "
          "flight",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answerOnce("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();
    f.manager->requestReversePairing(f.server);
    f.manager->forget(f.id);
    f.manager->pairWithPin(f.server, QStringLiteral("2222"));

    f.rest->releaseFirst("POST", kPairPath);
    REQUIRE(pumpUntil([&f] { return f.rest->delivered() == 1; }));

    REQUIRE(f.manager->isPairingInFlight(f.id));
    REQUIRE_FALSE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.state() == SessionState::Linking);
}

TEST_CASE("wifi manager: a 401 from before a Forget does not erase the key a fresh pair stored",
          "[manager][forget]") {
    ManagerFixture f;
    f.rest->answerOnce("PUT", kSessionsPath, CannedAnswer{401, R"({"code":"NOT_PAIRED"})"});
    f.rest->answerOnce("POST", kPairPath, CannedAnswer{200, kPairGranted});
    f.rest->hold();
    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    f.manager->forget(f.id);
    f.manager->pairWithPin(f.server, QStringLiteral("2222"));
    f.rest->releaseFirst("POST", kPairPath);
    REQUIRE(pumpUntil([&f] { return f.store->sharedKey(f.id).has_value(); }));

    f.rest->releaseFirst("PUT", kSessionsPath);
    REQUIRE(pumpUntil([&f] { return f.rest->delivered() == 2; }));

    REQUIRE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.events.empty());
}

// ---- A reply that lands after the user's Disconnect ----

TEST_CASE("wifi manager: a grant that lands after a user's Disconnect is handed back, not "
          "adopted",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});
    f.rest->hold();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    f.manager->disconnect(f.id);
    f.releaseAndDeliver();

    REQUIRE(f.effects.linkAttempts() == 0);
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.rest->count("DELETE", kGrantedSessionPath) == 1);
    REQUIRE_FALSE(f.remembers(f.server));
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: a refusal that lands after a user's Disconnect leaves the row as the user "
          "left it",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.rest->hold();

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);
    f.manager->disconnect(f.id);
    f.releaseAndDeliver();

    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.effects.pendingCalls() == 0);
    REQUIRE(f.events.empty());
}

// ---- A satellite whose certificate changed ----

TEST_CASE("wifi manager: a PIN pair with a satellite whose certificate changed says so, and does "
          "not blame the PIN",
          "[manager][identity]") {
    ManagerFixture f;
    f.presentChangedCertificate();

    f.manager->pairWithPin(f.server, QStringLiteral("1234"));
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.state() == SessionState::Idle);
    // Every reason the sheet can mark its field with would blame the PIN or the network.
    REQUIRE(f.pairingFailures.empty());
    requireOneError(f, kIdentityChangedMsg);
}

TEST_CASE("wifi manager: a user connect whose pair meets a changed certificate says so",
          "[manager][identity]") {
    ManagerFixture f;
    // No key on file, so the connect starts with a pair.
    f.store->forgetKey(f.id);
    f.presentChangedCertificate();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.state() == SessionState::Idle);
    requireOneError(f, kIdentityChangedMsg);
}

TEST_CASE("wifi manager: a silent connect whose pair meets a changed certificate stops without a "
          "word",
          "[manager][identity]") {
    ManagerFixture f;
    f.store->forgetKey(f.id);
    f.presentChangedCertificate();

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    // Idle, not Stale: Stale reads "Needs pairing", and no PIN gets past a changed certificate.
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: a user connect to a satellite whose certificate changed says so and does "
          "not retry",
          "[manager][identity]") {
    ManagerFixture f;
    f.presentChangedCertificate();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.state() == SessionState::Idle);
    requireOneError(f, kIdentityChangedMsg);
    requireNoRetry(f);
}

TEST_CASE("wifi manager: a silent reconnect to a satellite whose certificate changed stops without "
          "a word or a retry",
          "[manager][identity]") {
    ManagerFixture f;
    f.presentChangedCertificate();

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.settles());
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.events.empty());
    requireNoRetry(f);
}

TEST_CASE("wifi manager: a changed certificate ends the backoff, so a later failure starts it over",
          "[manager][identity]") {
    ManagerFixture f;
    f.failSilently();
    // The first silent retry meets the changed certificate.
    f.presentChangedCertificate();
    f.effects.advance(kPastFirstBackoffMs);
    REQUIRE(f.settles());
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 2);
    // The satellite has its own certificate back, and is turning connections away.
    f.http->setPinVerifier(&acceptsTheCertificate);
    f.rest->answerOnce("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);
    REQUIRE(f.settles());

    // Due after the first step of the curve, not the second.
    f.effects.advance(kPastFirstBackoffMs);

    REQUIRE(f.rest->count("PUT", kSessionsPath) == 4);
}

TEST_CASE("wifi manager: an approval request to a satellite whose certificate changed ends on the "
          "changed identity, not a decline",
          "[manager][identity]") {
    ManagerFixture f;
    f.presentChangedCertificate();

    f.manager->requestReversePairing(f.server);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::IdentityChanged);
    requireOneError(f, kIdentityChangedMsg);
}

TEST_CASE("wifi manager: an approval request to a satellite on another protocol version ends on "
          "the version, not a decline",
          "[manager][protocol]") {
    ManagerFixture f;
    f.rest->answer("POST", kPairPath, CannedAnswer{409, R"({"ok":false,"error":"protocol"})"});

    f.manager->requestReversePairing(f.server);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::VersionMismatch);
    requireOneError(f, kVersionMsg);
}

TEST_CASE("wifi manager: an approval request nobody answers says the satellite is unreachable",
          "[manager][reverse]") {
    ManagerFixture f;

    f.manager->requestReversePairing(f.server);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::TimedOut);
    requireOneError(f, kUnreachableMsg);
}

TEST_CASE("wifi manager: an approval poll that meets a changed certificate ends the attempt",
          "[manager][identity]") {
    ManagerFixture f;
    f.rest->answer("POST", kPairPath, CannedAnswer{200, R"({"ok":false,"pending":true})"});
    f.manager->requestReversePairing(f.server);
    REQUIRE(pumpUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));
    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::AwaitingApproval);
    f.presentChangedCertificate();

    // The poll runs on the manager's own one-second timer.
    REQUIRE(pumpUntil([&f] { return f.rest->count("GET", kStatusPath) == 1; }));
    REQUIRE(pumpUntil([&f] { return f.rest->allAnswered(); }));

    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::IdentityChanged);
    requireOneError(f, kIdentityChangedMsg);
}

// ---- Silent retries ----

TEST_CASE("wifi manager: a silent failure retries once its backoff has passed",
          "[manager][retry]") {
    ManagerFixture f;
    f.failSilently();
    REQUIRE(f.effects.pendingCalls() == 1);

    f.effects.advance(kPastFirstBackoffMs);

    REQUIRE(f.rest->count("PUT", kSessionsPath) == 2);
}

TEST_CASE("wifi manager: a user disconnect cancels the silent retry still waiting out its backoff",
          "[manager][retry]") {
    ManagerFixture f;
    f.failSilently();

    f.manager->disconnect(f.id);
    f.effects.advance(kPastFirstBackoffMs);

    REQUIRE(f.effects.scansStarted() == 0);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
    REQUIRE(f.state() == SessionState::Idle);
}

// ---- Discovery ----

TEST_CASE("wifi manager: a scan runs once at a time and publishes what it found",
          "[manager][discovery]") {
    ManagerFixture f;
    int published = 0;
    QObject::connect(f.manager.get(), &dish::net::WifiConnectionManager::discoveredChanged,
                     f.manager.get(), [&published] { ++published; });

    f.manager->startDiscovery();
    f.manager->startDiscovery();
    REQUIRE(f.effects.scansStarted() == 1);
    REQUIRE(f.manager->isScanning());
    f.effects.finishScans({f.server});

    REQUIRE_FALSE(f.manager->isScanning());
    REQUIRE(published == 1);
    REQUIRE(f.manager->discoveredServers().size() == 1);
}

// ---- A user disconnect sticks until the user connects again ----

TEST_CASE("wifi manager: the periodic reconnect leaves a satellite the user disconnected alone",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.store->remember(f.server);
    f.failSilently();

    f.manager->disconnect(f.id);
    f.manager->autoReconnectAll();

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 1);
}

TEST_CASE("wifi manager: a finished scan does not reconnect a satellite the user disconnected",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.store->remember(f.server);
    f.failSilently();
    f.manager->disconnect(f.id);

    f.manager->startDiscovery();
    f.effects.finishScans({f.server});

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 1);
}

TEST_CASE("wifi manager: the periodic reconnect still reaches a satellite the user did not "
          "disconnect",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.store->remember(f.server);
    f.store->remember(f.other);
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);
    REQUIRE(f.settles());

    f.manager->disconnect(f.id);
    f.manager->autoReconnectAll();

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 1);
    REQUIRE(f.sessionPutsTo(kOtherPrivateIp) == 1);
}

TEST_CASE("wifi manager: a user connect lifts the hold a user disconnect put on reconnecting",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.store->remember(f.server);
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.manager->disconnect(f.id);
    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    REQUIRE(f.settles());

    f.manager->autoReconnectAll();

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 2);
}

TEST_CASE("wifi manager: forgetting a satellite lifts the hold a user disconnect put on it",
          "[manager][disconnect]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});
    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);
    REQUIRE(f.settles());
    f.manager->disconnect(f.id);
    f.manager->forget(f.id);

    f.pair(f.server);
    f.store->remember(f.server);
    f.manager->autoReconnectAll();

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 2);
}

TEST_CASE("wifi manager: a sleep and resume is not a user disconnect", "[manager][disconnect]") {
    ManagerFixture f;
    f.store->remember(f.server);
    f.failSilently();

    f.manager->prepareForSleep();
    f.manager->resumeFromSleep();

    REQUIRE(f.sessionPutsTo(kPrivateIp) == 2);
}

// ---- A 409 with overlapping ranges re-offers the lower version at once ----

TEST_CASE("wifi manager: a user connect re-offers the satellite's version at once after a 409",
          "[manager][protocol]") {
    ManagerFixture f;
    f.rest->answerOnce("PUT", kSessionsPath, CannedAnswer{409, versionConflict(kOneBehind)});
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    REQUIRE(pumpUntil([&f] { return f.rest->count("PUT", kSessionsPath) == 2; }));
    REQUIRE(f.settles());

    const auto puts = f.sessionPuts();
    REQUIRE(offeredVersionOf(puts[0]) == dish::proto::kProtocolVersion);
    REQUIRE(offeredVersionOf(puts[1]) == kOneBehind);
    requireOneError(f, kUnreachableMsg);
}

TEST_CASE("wifi manager: a silent connect re-offers the satellite's version without a backoff",
          "[manager][protocol]") {
    ManagerFixture f;
    f.rest->answerOnce("PUT", kSessionsPath, CannedAnswer{409, versionConflict(kOneBehind)});
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{503, kShuttingDown});

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(pumpUntil([&f] { return f.rest->count("PUT", kSessionsPath) == 2; }));
    REQUIRE(offeredVersionOf(f.sessionPuts()[1]) == kOneBehind);
}

TEST_CASE("wifi manager: a 409 that names no lower version ends the attempt instead of looping",
          "[manager][protocol]") {
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath,
                   CannedAnswer{409, versionConflict(dish::proto::kProtocolVersion)});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.effects.pendingCalls() == 0);
    requireOneError(f, kVersionMsg);
}

TEST_CASE("wifi manager: a user disconnect while the 409 is in flight is not undone by a re-offer",
          "[manager][protocol]") {
    ManagerFixture f;
    f.rest->answerOnce("PUT", kSessionsPath, CannedAnswer{409, versionConflict(kOneBehind)});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    f.manager->disconnect(f.id);
    REQUIRE(pumpUntil([&f] { return f.rest->allAnswered(); }));

    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
    REQUIRE(f.state() == SessionState::Idle);
    REQUIRE(f.events.empty());
}

// ---- The satellite is IPv4 only ----

TEST_CASE("wifi manager: a user connect to an IPv6 satellite is refused before any request",
          "[manager][ipv6]") {
    ManagerFixture f(kIpv6Ip);

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.rest->requests().empty());
    REQUIRE(f.manager->get(f.id) == nullptr);
    requireOneError(f, kIpv6Msg.arg(kIpv6Ip));
}

TEST_CASE("wifi manager: a silent connect to a remembered IPv6 satellite is refused quietly",
          "[manager][ipv6]") {
    ManagerFixture f(kIpv6Ip);

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.rest->requests().empty());
    REQUIRE(f.events.empty());
}

TEST_CASE("wifi manager: pairing with an IPv6 satellite is refused before any request",
          "[manager][ipv6]") {
    ManagerFixture f(kIpv6Ip);

    f.manager->pairWithPin(f.server, QStringLiteral("1234"));

    REQUIRE_FALSE(f.manager->isPairingInFlight(f.id));
    REQUIRE(f.pairingFailures == std::vector<QString>{QStringLiteral("unreachable")});
    requireOneError(f, kIpv6Msg.arg(kIpv6Ip));
}

TEST_CASE("wifi manager: an approval request to an IPv6 satellite is refused before any request",
          "[manager][ipv6]") {
    ManagerFixture f(kIpv6Ip);

    f.manager->requestReversePairing(f.server);

    REQUIRE_FALSE(f.manager->isPairingInFlight(f.id));
    REQUIRE(f.manager->reversePairingPhase() == ReversePairingPhase::Idle);
    requireOneError(f, kIpv6Msg.arg(kIpv6Ip));
}

// ---- What the user reads is in their language ----

TEST_CASE("wifi manager: the grant failure a user is told of reads in their language",
          "[manager][grant][i18n]") {
    if (!dish::test::catalogsBuilt()) { SKIP("built without Qt LinguistTools"); }
    const InstalledCatalog german(QStringLiteral("de_DE"));
    REQUIRE(german.loaded);
    const QString inGerman = german.lookup(kManagerContext, kWireFailedMsg);
    REQUIRE_FALSE(inGerman.isEmpty());
    ManagerFixture f;
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    requireOneError(f, inGerman);
}

TEST_CASE("wifi manager: the changed-certificate message reads in the user's language",
          "[manager][identity][i18n]") {
    if (!dish::test::catalogsBuilt()) { SKIP("built without Qt LinguistTools"); }
    const InstalledCatalog german(QStringLiteral("de_DE"));
    REQUIRE(german.loaded);
    const QString inGerman = german.lookup(kManagerContext, kIdentityChangedMsg);
    REQUIRE_FALSE(inGerman.isEmpty());
    ManagerFixture f;
    f.presentChangedCertificate();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    requireOneError(f, inGerman);
}

TEST_CASE("wifi manager: the IPv6 refusal reads in the user's language", "[manager][ipv6][i18n]") {
    if (!dish::test::catalogsBuilt()) { SKIP("built without Qt LinguistTools"); }
    const InstalledCatalog german(QStringLiteral("de_DE"));
    REQUIRE(german.loaded);
    const QString inGerman = german.lookup(kManagerContext, kIpv6Msg);
    REQUIRE_FALSE(inGerman.isEmpty());
    ManagerFixture f(kIpv6Ip);

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    requireOneError(f, inGerman.arg(kIpv6Ip));
}

// ---- A pin with no pairing behind it ----

namespace {

const QString kLoopback = QStringLiteral("127.0.0.1");

QString fingerprintOf(const QByteArray& certDer) {
    return QString::fromStdString(
        dish::net::sha256FingerprintHex(reinterpret_cast<const std::uint8_t*>(certDer.constData()),
                                        static_cast<std::size_t>(certDer.size())));
}

// The satellite's answers: a pair gets a key, and anything else is turned away.
dish::test::HttpsAnswer grantingThePair(const dish::test::SeenRequest& r) {
    if (r.path == kPairPath) { return dish::test::HttpsAnswer{200, kPairGranted}; }
    return dish::test::HttpsAnswer{503, kShuttingDown};
}

// The manager over a real HTTPClient, dialling a satellite that answers HTTPS on loopback, so the
// manager's own pin verifier meets a real certificate.
struct TlsManagerFixture {
    dish::test::FakeHttpsListener listener;
    DiscoveredServer server = satelliteAt(kLoopback, QStringLiteral("m-tls"));
    QString id = dish::net::WifiConnection::idFor(server);
    std::shared_ptr<QSettings> settings = dish::test::makeSharedSettings();
    std::unique_ptr<dish::net::ConnectionStore> store;
    FakeWifiEffects effects;
    std::unique_ptr<dish::net::WifiConnectionManager> manager;
    std::vector<ConnectionEvent> events;

    TlsManagerFixture() {
        listener.respond = &grantingThePair;
        server.pairPort = listener.port();
        server.httpPort = listener.port();
        store = std::make_unique<dish::net::ConnectionStore>(
            std::make_unique<QSettings>(settings->fileName(), QSettings::IniFormat));
        manager = std::make_unique<dish::net::WifiConnectionManager>(
            store.get(), new dish::net::HTTPClient(nullptr), effects.effects(), nullptr);
        QObject::connect(manager.get(), &dish::net::WifiConnectionManager::connectionEvent,
                         manager.get(), [this](const ConnectionEvent& e) { events.push_back(e); });
    }

    // What an older install of this satellite presented, pinned for its address.
    void pinAnOlderCertificate() {
        store->facade().pins().pin(kLoopback, QString(64, QLatin1Char('0')));
    }

    QString pinnedFingerprint() const {
        return store->facade().pins().pinnedFingerprint(kLoopback).value_or(QString());
    }

    bool toldTheIdentityChanged() const {
        return std::any_of(events.begin(), events.end(), [](const ConnectionEvent& e) {
            return e.message == kIdentityChangedMsg;
        });
    }
};

} // namespace

TEST_CASE("wifi manager: a satellite pinned before it ever paired, then reinstalled, pairs again",
          "[manager][identity][tls]") {
    REQUIRE(dish::test::useTestTlsBackend());
    TlsManagerFixture f;
    REQUIRE(f.listener.listening());
    f.pinAnOlderCertificate();

    f.manager->pairWithPin(f.server, QStringLiteral("1234"));
    REQUIRE(dish::test::spinUntil([&f] { return !f.manager->isPairingInFlight(f.id); }));

    REQUIRE(f.store->sharedKey(f.id).has_value());
    REQUIRE(f.pinnedFingerprint() == fingerprintOf(f.listener.certDer()));
    REQUIRE_FALSE(f.toldTheIdentityChanged());
}

TEST_CASE("wifi manager: a satellite with a key on file whose certificate changed is still refused",
          "[manager][identity][tls]") {
    REQUIRE(dish::test::useTestTlsBackend());
    TlsManagerFixture f;
    REQUIRE(f.listener.listening());
    f.store->setSharedKey(QString(static_cast<int>(kPairingKeySize) * 2, QLatin1Char('1')), f.id);
    f.pinAnOlderCertificate();

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);
    REQUIRE(dish::test::spinUntil(
        [&f] { return f.manager->get(f.id)->state() != SessionState::Linking; }));

    REQUIRE(f.listener.requests().empty());
    REQUIRE(f.pinnedFingerprint() == QString(64, QLatin1Char('0')));
    REQUIRE(f.toldTheIdentityChanged());
}
