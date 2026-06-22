// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Remembered-satellite reconnect / relearn (bug: "I have to scan and have it
// discovered, then I can connect again — no PIN required"). The fix wires
// WifiConnectionManager::startDiscovery to persist a moved satellite's IP back
// to the remembered store via the net::ConnectionStore adapter's
// refreshFromDiscovery passthrough (mirroring dish-android
// SatelliteConnectionManager.startDiscovery's store.refreshFromDiscovery), so
// the next autoReconnectAll / silent backoff retry — both of which read
// remembered().toDiscovered() — target the CURRENT address instead of a stale
// one. The durable persistence rules themselves are pinned in
// test_connection_store_identity (the repository facade); here we pin the
// behaviour at the EXACT seam the session manager calls — the net::Connection
// Store adapter — plus the reconnect-relevant invariants the manager relies on:
//   * a relearn re-points an already-remembered row's endpoint in place,
//   * remembered().toDiscovered() then yields the fresh ip/ports (what
//     autoReconnectAll / scheduleRetry feed into connectTo),
//   * a relearn never adds an un-remembered satellite,
//   * the pairing key survives (keyed on the stable machineId id) so no PIN is
//     needed on the eventual reconnect.
//
// The manager's full async connect FSM can't be unit-driven without sockets
// (no injectable HTTP gateway seam — see test_session_manager.cpp), so these
// pin the relearn DATA contract the wiring depends on, which is the part that
// was missing.

#include "Network/ConnectionStore.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QSettings>
#include <QString>
#include <QUuid>

#include <memory>

using dish::models::DiscoveredServer;
using dish::models::RememberedWifi;

namespace {

DiscoveredServer server(const QString& machineId, const QString& ip,
                        const QString& name = QStringLiteral("Pc"), int udpPort = 9876,
                        int pairPort = 9443, int httpPort = 9443) {
    DiscoveredServer s;
    s.machineId = machineId;
    s.ip = ip;
    s.name = name;
    s.udpPort = udpPort;
    s.pairPort = pairPort;
    s.httpPort = httpPort;
    return s;
}

QString midId(const QString& machineId) { return QStringLiteral("mid:") + machineId; }

// A fresh net::ConnectionStore adapter (the exact seam WifiConnectionManager
// owns) backed by an isolated temp INI — never the real registry. The adapter
// adopts the unique_ptr<QSettings> as the shared backing store.
dish::net::ConnectionStore makeStore() {
    const QString path = QDir::tempPath() + QStringLiteral("/dish-reconnect-") +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) +
                         QStringLiteral(".ini");
    return dish::net::ConnectionStore(std::make_unique<QSettings>(path, QSettings::IniFormat));
}

const QString kKeyAA = QString(64, QLatin1Char('a'));

// Find a remembered row by id (or a default-constructed one if absent).
RememberedWifi rowFor(const dish::net::ConnectionStore& store, const QString& id) {
    for (const auto& r : store.remembered()) {
        if (r.id == id) { return r; }
    }
    return {};
}

} // namespace

TEST_CASE("relearn re-points a remembered satellite's endpoint in place (adapter seam)",
          "[reconnect]") {
    auto store = makeStore();
    store.remember(server("m1", "10.0.0.5"));
    REQUIRE(store.remembered().size() == 1);
    CHECK(rowFor(store, midId("m1")).ip == QStringLiteral("10.0.0.5"));

    // A scan finds the same box at a new DHCP lease — the manager forwards the
    // discovery list to refreshFromDiscovery.
    store.refreshFromDiscovery({server("m1", "10.0.0.99")});

    REQUIRE(store.remembered().size() == 1);
    CHECK(rowFor(store, midId("m1")).ip == QStringLiteral("10.0.0.99"));
}

TEST_CASE("after a relearn, toDiscovered yields the fresh endpoint autoReconnect would dial",
          "[reconnect]") {
    auto store = makeStore();
    store.remember(server("m1", "10.0.0.5", QStringLiteral("Den"), 9876, 9443, 9443));
    store.refreshFromDiscovery(
        {server("m1", "10.0.0.99", QStringLiteral("Den"), 9876, 9444, 9445)});

    // autoReconnectAll / scheduleRetry both do: remembered() -> toDiscovered() ->
    // connectTo. That server must carry the CURRENT address + ports.
    const DiscoveredServer dialed = rowFor(store, midId("m1")).toDiscovered();
    CHECK(dialed.ip == QStringLiteral("10.0.0.99"));
    CHECK(dialed.pairPort == 9444);
    CHECK(dialed.httpPort == 9445);
    CHECK(dialed.machineId == QStringLiteral("m1"));
    CHECK(dialed.isValid());
    // Same stable id before and after — the connection/key keep matching.
    CHECK(dialed.id() == midId("m1"));
}

TEST_CASE("a relearn never adds an un-remembered satellite", "[reconnect]") {
    auto store = makeStore();
    store.remember(server("m1", "10.0.0.5"));
    // The scan also saw a never-paired box; it must not be remembered.
    store.refreshFromDiscovery({server("m1", "10.0.0.99"), server("m2", "10.0.0.6")});

    const auto rows = store.remembered();
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].id == midId("m1"));
    CHECK(rows[0].ip == QStringLiteral("10.0.0.99"));
}

TEST_CASE("the pairing key survives a relearn (no PIN needed on reconnect)", "[reconnect]") {
    auto store = makeStore();
    store.remember(server("m1", "10.0.0.5"));
    // The key is keyed on the stable id (machineId), independent of IP.
    store.setSharedKey(kKeyAA, midId("m1"));

    store.refreshFromDiscovery({server("m1", "10.0.0.99")});

    // The key still resolves under the same id after the address change — this
    // is why credentialsFor() succeeds and connectTo skips the pair handshake.
    CHECK(store.sharedKey(midId("m1")) == kKeyAA);
    CHECK(rowFor(store, midId("m1")).ip == QStringLiteral("10.0.0.99"));
}

TEST_CASE("a beacon without a machineId never re-points a remembered row", "[reconnect]") {
    auto store = makeStore();
    store.remember(server("m1", "10.0.0.5"));
    // A machineId-less beacon at a different address must not move the row (it
    // can't be matched to the stable identity).
    store.refreshFromDiscovery({server("", "10.0.0.99")});
    CHECK(rowFor(store, midId("m1")).ip == QStringLiteral("10.0.0.5"));
}
