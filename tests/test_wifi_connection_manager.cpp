// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Drives the real WifiConnectionManager against a satellite whose REST API is
// answered in-process (FakeSatelliteRest.h), so every REST call it makes is
// seen and no packet leaves the process.

#include "Network/ConnectionStore.h"
#include "Network/HTTPClient.h"
#include "Network/SatelliteClient.h"
#include "Network/WifiConnection.h"
#include "Network/WifiConnectionManager.h"
#include "core/net/IpLiterals.h"
#include "core/wire/SessionCrypto.h"
#include "Util/Localization.h"

#include "FakeSatelliteRest.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QLocale>
#include <QSettings>
#include <QString>
#include <QTranslator>

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
using dish::net::SessionState;
using dish::test::CannedAnswer;
using dish::test::FakeSatelliteRest;

namespace {

constexpr int kHttpPort = 9443;
constexpr int kUdpPort = 9876;
constexpr std::uint8_t kPairingKeyByte = 0x11;
constexpr std::size_t kPairingKeySize = 32;
constexpr int kReplyTimeoutMs = 5000;
// Past the first silent retry's 1 s backoff (reducer::backoffDelayMs(1)).
constexpr int kPastFirstBackoffMs = 1300;

const QString kSessionsPath = QStringLiteral("/api/connections");
const QString kGrantedSessionPath = QStringLiteral("/api/connections/conn_1");

const QString kWireFailedMsg =
    QStringLiteral("The satellite accepted, but the controller link would not open. Try again.");

const QByteArray kGrant = R"({"connectionId":"conn_1","token":"00000001",)"
                          R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortToken = R"({"connectionId":"conn_1","token":"0001",)"
                                        R"("sessionSalt":"0102030405060708","epoch":1})";
const QByteArray kGrantWithShortSalt = R"({"connectionId":"conn_1","token":"00000001",)"
                                       R"("sessionSalt":"0102","epoch":1})";

// WifiConnectionManager builds its HTTPClient on QNetworkAccessManager, which
// needs a QCoreApplication; Catch2WithMain creates none. The function-local
// static with a leaked argv keeps one alive for the process.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

DiscoveredServer satelliteAt(const QString& ip) {
    DiscoveredServer s;
    s.machineId = QStringLiteral("m1");
    s.ip = ip;
    s.name = QStringLiteral("Pc");
    s.udpPort = kUdpPort;
    s.pairPort = kHttpPort;
    s.httpPort = kHttpPort;
    return s;
}

bool pumpUntil(const std::function<bool()>& done) {
    const QDeadlineTimer deadline(kReplyTimeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) { return false; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    return true;
}

void pumpFor(int ms) {
    const QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) { QCoreApplication::processEvents(QEventLoop::AllEvents, 10); }
}

// One paired satellite, a manager wired to the in-process REST fake, and every
// event the manager raised.
struct ManagerFixture {
    DiscoveredServer server;
    QString id;
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<dish::net::ConnectionStore> store;
    FakeSatelliteRest* rest = nullptr; // owned by the manager's HTTPClient
    std::unique_ptr<dish::net::WifiConnectionManager> manager;
    std::vector<ConnectionEvent> events;

    explicit ManagerFixture(const QString& ip)
        : server(satelliteAt(ip)), id(dish::net::WifiConnection::idFor(server)),
          settings(dish::test::makeSharedSettings()) {
        ensureApp();
        store = std::make_unique<dish::net::ConnectionStore>(
            std::make_unique<QSettings>(settings->fileName(), QSettings::IniFormat));
        store->setSharedKey(QString(static_cast<int>(kPairingKeySize) * 2, QLatin1Char('1')), id);
        rest = new FakeSatelliteRest;
        manager = std::make_unique<dish::net::WifiConnectionManager>(
            store.get(), new dish::net::HTTPClient(rest, nullptr), nullptr);
        QObject::connect(manager.get(), &dish::net::WifiConnectionManager::connectionEvent,
                         manager.get(), [this](const ConnectionEvent& e) { events.push_back(e); });
    }

    SessionState state() const { return manager->get(id)->state(); }

    bool settles() {
        return pumpUntil([this] { return state() != SessionState::Linking; });
    }

    std::string expectedProof() const {
        std::array<std::uint8_t, kPairingKeySize> key{};
        key.fill(kPairingKeyByte);
        const std::string deviceId = store->getOrCreateDeviceId().toStdString();
        return dish::wire::computeHmacProof(key.data(), deviceId);
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

// A catalogue installed on the app for one test's lifetime, as main.cpp installs
// the user's.
struct InstalledCatalog {
    QTranslator translator;
    bool loaded = false;

    explicit InstalledCatalog(const QString& localeName) {
        ensureApp();
        loaded =
            dish::i18n::loadCatalog(translator, QLocale(localeName), QStringLiteral(DISH_QM_DIR));
        if (loaded) { QCoreApplication::installTranslator(&translator); }
    }
    ~InstalledCatalog() { QCoreApplication::removeTranslator(&translator); }
    InstalledCatalog(const InstalledCatalog&) = delete;
    InstalledCatalog& operator=(const InstalledCatalog&) = delete;
    InstalledCatalog(InstalledCatalog&&) = delete;
    InstalledCatalog& operator=(InstalledCatalog&&) = delete;
};

void requireWireFailureReported(const ManagerFixture& f) {
    REQUIRE(f.events.size() == 1);
    REQUIRE(f.events.front().kind == ConnectionEventKind::Error);
    REQUIRE(f.events.front().message.toStdString() == kWireFailedMsg.toStdString());
}

} // namespace

TEST_CASE("wifi manager: the controller link cannot open to an IPv6 satellite",
          "[manager][grant]") {
    // The premise the socket-failure cases rest on: openSocket is AF_INET only,
    // while the connect guard accepts a private IPv6 literal.
    REQUIRE(dish::net::isPrivateHostLiteral("[fd00::5]"));
    dish::net::SatelliteClient client;
    REQUIRE_FALSE(client.openSocket("[fd00::5]", kUdpPort));
}

TEST_CASE("wifi manager: a user connect whose socket will not open reports it and releases the "
          "granted session",
          "[manager][grant]") {
    ManagerFixture f(QStringLiteral("[fd00::5]"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    requireWireFailureReported(f);
}

TEST_CASE("wifi manager: a silent connect whose socket will not open releases the granted session "
          "quietly and does not retry",
          "[manager][grant]") {
    ManagerFixture f(QStringLiteral("[fd00::5]"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    REQUIRE(f.events.empty());
    pumpFor(kPastFirstBackoffMs);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
}

TEST_CASE("wifi manager: a granted token of the wrong length is reported and the session released",
          "[manager][grant]") {
    ManagerFixture f(QStringLiteral("10.0.0.5"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortToken});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    requireWireFailureReported(f);
}

TEST_CASE("wifi manager: a granted salt of the wrong length is reported and the session released",
          "[manager][grant]") {
    ManagerFixture f(QStringLiteral("10.0.0.5"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortSalt});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    requireWireFailureReported(f);
}

TEST_CASE("wifi manager: a silent connect with unusable material releases the session quietly",
          "[manager][grant]") {
    ManagerFixture f(QStringLiteral("10.0.0.5"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrantWithShortToken});

    f.manager->connectTo(f.server, ConnectIntent::AutoReconnect);

    REQUIRE(f.settles());
    requireGrantReleased(f);
    REQUIRE(f.events.empty());
    pumpFor(kPastFirstBackoffMs);
    REQUIRE(f.rest->count("PUT", kSessionsPath) == 1);
}

TEST_CASE("wifi manager: the grant failure a user is told of reads in their language",
          "[manager][grant][i18n]") {
    if (!QFile::exists(QStringLiteral(DISH_QM_DIR "/dish_de.qm"))) {
        SKIP("built without Qt LinguistTools, so there is no catalogue to read");
    }
    const InstalledCatalog german(QStringLiteral("de_DE"));
    REQUIRE(german.loaded);
    const QString inGerman =
        german.translator.translate("dish::net::WifiConnectionManager", kWireFailedMsg.toUtf8());
    REQUIRE_FALSE(inGerman.isEmpty());
    ManagerFixture f(QStringLiteral("[fd00::5]"));
    f.rest->answer("PUT", kSessionsPath, CannedAnswer{200, kGrant});

    f.manager->connectTo(f.server, ConnectIntent::UserInitiated);

    REQUIRE(f.settles());
    REQUIRE(f.events.size() == 1);
    REQUIRE(f.events.front().message.toStdString() == inGerman.toStdString());
}
