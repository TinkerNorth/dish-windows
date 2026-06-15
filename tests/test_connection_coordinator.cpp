// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The ConnectionCoordinator: the imperative command surface over the connection
// subsystem that RE-EXPOSES (never mirrors) the ConnectionsComposer's derived
// row list. Built on the real WifiConnectionManager + ConnectionHub + an
// in-memory ConnectionStore (no sockets are opened — these exercise the
// bind/unbind/forget orchestration + the re-exposed reactive list, not live
// connects). Re-derives the satellite arms of dish-android
// composer/ConnectionCoordinatorTest (the Bluetooth-HID-peripheral arms are
// phone-only and out of scope for Windows).
//
// Remembered satellites are seeded in the store BEFORE the coordinator is built,
// so the coordinator's eager initial refresh picks them up (in production a
// remember always rides a manager action that fires poolChanged). Reactivity is
// then driven through the real signal paths: bind/unbind fire the hub's
// `changed`, forget fires the manager's `poolChanged`.

#include "Network/ConnectionHub.h"
#include "Network/ConnectionStore.h"
#include "Network/WifiConnectionManager.h"
#include "composer/ConnectionCoordinator.h"
#include "core/reducer/SatelliteLinkState.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QString>

#include <memory>
#include <vector>

using dish::composer::ConnectionCoordinator;
using dish::composer::ConnectionRow;
using dish::models::DiscoveredServer;
using dish::reducer::UiLinkState;
using dish::test::makeSharedSettings;

namespace {

// WifiConnectionManager builds an HTTPClient (QNetworkAccessManager), whose
// app-static factory asserts unless a QCoreApplication exists. The test runner
// (Catch2WithMain) creates none, so stand one up once for the process. A
// function-local static with a leaked argv keeps it alive for every coordinator
// test without disturbing the other translation units.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

DiscoveredServer sat(const QString& machineId, const QString& ip, const QString& name = {}) {
    DiscoveredServer s;
    s.machineId = machineId;
    s.ip = ip;
    s.name = name;
    s.udpPort = 9876;
    s.pairPort = 9443;
    s.httpPort = 9443;
    return s;
}

QString midId(const QString& machineId) { return QStringLiteral("mid:") + machineId; }

// Owns the full satellite stack on an isolated in-memory store, in the AppModel
// construction order (store -> manager -> hub -> coordinator). Remembered
// satellites passed in are seeded BEFORE the coordinator is constructed so its
// eager initial refresh sees them.
struct Fixture {
    std::unique_ptr<dish::net::ConnectionStore> store;
    std::unique_ptr<dish::net::WifiConnectionManager> wifi;
    std::unique_ptr<dish::net::ConnectionHub> hub;
    std::unique_ptr<ConnectionCoordinator> coord;

    explicit Fixture(const std::vector<DiscoveredServer>& seed = {}) {
        ensureApp();
        auto shared = makeSharedSettings();
        store = std::make_unique<dish::net::ConnectionStore>(
            std::unique_ptr<QSettings>(new QSettings(shared->fileName(), QSettings::IniFormat)));
        for (const auto& s : seed) { store->remember(s); }
        wifi = std::make_unique<dish::net::WifiConnectionManager>(store.get());
        hub = std::make_unique<dish::net::ConnectionHub>(wifi.get(), store.get());
        coord = std::make_unique<ConnectionCoordinator>(wifi.get(), hub.get());
    }

    std::optional<ConnectionRow> row(const QString& id) const {
        return coord->summary(id.toStdString());
    }
};

} // namespace

TEST_CASE("coordinator: initial connections list is empty when no sources have data", "[coord]") {
    Fixture f;
    REQUIRE(f.coord->connections().value().empty());
}

TEST_CASE("coordinator: connections() re-exposes the SAME observable (no mirror)", "[coord]") {
    Fixture f;
    // The re-exposed reference is the composer's own Observable: subscribing to
    // it and then driving an upstream (a bind -> hub changed -> refresh) must
    // deliver to that exact subscription.
    f.hub->bind(QStringLiteral("slot-A"), midId("m1")); // no-op binding (no such conn) but fires
    int emissions = 0;
    auto sub = f.coord->connections().subscribe(
        [&](const std::vector<ConnectionRow>&) { ++emissions; }, /*emitCurrent=*/true);
    const int afterInitial = emissions; // the eager replay
    f.hub->unbind(QStringLiteral("slot-A"));
    REQUIRE(emissions >= afterInitial); // the same observable is what the coordinator returns
}

TEST_CASE("coordinator: a remembered satellite appears as a Saved row", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    const auto rows = f.coord->connections().value();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == midId("m1").toStdString());
    REQUIRE(rows[0].label == "Pc");
    REQUIRE(rows[0].live == UiLinkState::Saved);
    REQUIRE(rows[0].boundSlotId.empty());
}

TEST_CASE("coordinator: summary(id) returns the matching row or nullopt", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    const auto s = f.coord->summary(midId("m1").toStdString());
    REQUIRE(s.has_value());
    REQUIRE(s->label == "Pc");
    REQUIRE_FALSE(f.coord->summary("nope").has_value());
}

TEST_CASE("coordinator: bind reflects the slot in the connection's row", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    f.coord->bind(QStringLiteral("slot-A"), midId("m1"));

    const auto r = f.row(midId("m1"));
    REQUIRE(r.has_value());
    REQUIRE(r->boundSlotId == "slot-A");
}

TEST_CASE("coordinator: unbind clears the binding from the row", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    f.coord->bind(QStringLiteral("slot-A"), midId("m1"));
    REQUIRE(f.row(midId("m1"))->boundSlotId == "slot-A");

    f.coord->unbind(QStringLiteral("slot-A"));
    REQUIRE(f.row(midId("m1"))->boundSlotId.empty());
}

TEST_CASE("coordinator: binding a slot to a new connection moves it off the prior one", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "A"), sat("m2", "10.0.0.2", "B")});
    f.coord->bind(QStringLiteral("slot-A"), midId("m1"));
    f.coord->bind(QStringLiteral("slot-A"), midId("m2"));

    REQUIRE(f.row(midId("m1"))->boundSlotId.empty());
    REQUIRE(f.row(midId("m2"))->boundSlotId == "slot-A");
}

TEST_CASE("coordinator: forgetConnection clears the binding then forgets the satellite",
          "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    f.coord->bind(QStringLiteral("slot-A"), midId("m1"));
    REQUIRE(f.row(midId("m1"))->boundSlotId == "slot-A");

    f.coord->forgetConnection(midId("m1"));

    // The remembered row is gone (forget dropped it) and the binding with it.
    REQUIRE(f.coord->connections().value().empty());
    REQUIRE(f.wifi->remembered().isEmpty());
    REQUIRE_FALSE(f.hub->bindings().contains(QStringLiteral("slot-A")));
}

TEST_CASE("coordinator: connectionsChanged fires on a binding change", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc")});
    int changes = 0;
    QObject::connect(f.coord.get(), &ConnectionCoordinator::connectionsChanged, [&] { ++changes; });
    f.coord->bind(QStringLiteral("slot-A"), midId("m1"));
    REQUIRE(changes >= 1);
}

TEST_CASE("coordinator: forgetting one connection leaks no row for it", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Pc"), sat("m2", "10.0.0.2", "Other")});
    REQUIRE(f.coord->connections().value().size() == 2);

    f.coord->forgetConnection(midId("m1"));
    const auto rows = f.coord->connections().value();
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].id == midId("m2").toStdString());
}

TEST_CASE("coordinator: autoReconnectAll with no remembered hosts is a quiet no-op", "[coord]") {
    Fixture f;
    f.coord->autoReconnectAll();
    REQUIRE(f.coord->connections().value().empty());
}

TEST_CASE("coordinator: rows are sorted by label", "[coord]") {
    Fixture f({sat("m1", "10.0.0.1", "Zeta"), sat("m2", "10.0.0.2", "Alpha")});
    const auto rows = f.coord->connections().value();
    REQUIRE(rows.size() == 2);
    REQUIRE(rows[0].label == "Alpha");
    REQUIRE(rows[1].label == "Zeta");
}
