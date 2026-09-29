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
#include "core/wire/SessionCrypto.h"

#include "FakeSatelliteRest.h"
#include "FakeWifiEffects.h"
#include "InstalledCatalog.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QString>

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

const QByteArray kGrant = R"({"connectionId":"conn_1","token":"00000001",)"
                          R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortToken = R"({"connectionId":"conn_1","token":"0001",)"
                                        R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortSalt = R"({"connectionId":"conn_1","token":"00000001",)"
                                       R"("sessionSalt":"0102","epoch":1})";
const QByteArray kShuttingDown = R"({"code":"SHUTTING_DOWN"})";

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
    FakeSatelliteRest* rest = nullptr; // owned by the manager's HTTPClient
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
        manager = std::make_unique<dish::net::WifiConnectionManager>(
            store.get(), new dish::net::HTTPClient(rest, nullptr), effects.effects(), nullptr);
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
