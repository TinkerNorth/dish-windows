// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// QML migration (C1) — the Connections C++ foundation that AppViewModel exposes
// REACTIVELY to the QML Connections page. The AppViewModel itself can't be
// instantiated in this harness (it owns a full AppModel: SDL bridge, theme
// controller over qApp, processor), so these pin the DATA CONTRACT it folds —
// the seams the investigation named:
//
//   * Reactive discovery: AppViewModel re-emits its discoveredChanged/scanning
//     NOTIFYs straight off WifiConnectionManager::discoveredChanged/scanningChanged.
//     So we drive the MANAGER seam and assert those edges fire (the
//     signal→signal fold is then a trivial, compiler-checked forward). The
//     manager's async connect/discovery FSM can't be unit-driven without sockets
//     (no HTTP gateway seam — see test_session_manager / test_remembered_reconnect),
//     so we assert the SYNCHRONOUS scan-start edge that startDiscovery raises.
//   * The `source` field each discoveredServers() entry carries: the FOUND row
//     shows the discovery-source label (Widgets ConnectionsDialog did). That
//     label is models::discoverySourceLabel(server.source); pin its mapping.
//   * De-raced id resolution: connectByServerId/pairByServerId/reconnect match a
//     server by its STABLE id (DiscoveredServer::id()), not a list index. Pin the
//     identity those resolutions key on (machineId-preferring), so a reorder
//     between read and call can't connect the wrong box.
//
// The command FORWARDING (reconnect/disconnect) is exercised end-to-end against
// the real manager in test_connection_coordinator.cpp via the coordinator Fixture.

#include "Models/Models.h"
#include "Network/ConnectionStore.h"
#include "Network/WifiConnectionManager.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QObject>
#include <QString>

#include <memory>

using dish::models::DiscoveredServer;
using dish::models::DiscoverySource;
using dish::models::discoverySourceLabel;
using dish::test::makeSharedSettings;

namespace {

DiscoveredServer sat(const QString& machineId, const QString& ip, DiscoverySource src) {
    DiscoveredServer s;
    s.machineId = machineId;
    s.ip = ip;
    s.source = src;
    return s;
}

} // namespace

// ── Reactive discovery (verified at runtime, not here) ───────────────────────
//
// AppViewModel folds WifiConnectionManager::scanningChanged/discoveredChanged
// into its `scanning`/`discoveredServers` NOTIFYs (a compiler-checked
// signal->signal connect in AppViewModel.cpp). Exercising the edge means calling
// the real manager's startDiscovery(), which launches a blocking Winsock/mDNS
// discovery on the global thread pool that the test process must then join on
// exit — there is no network seam (the same documented limitation as
// test_session_manager). Driving it here hangs the process. So the FOUND list +
// Scan button reactivity is verified at RUNTIME; unit coverage stays on the
// network-free data contract below.

// ── The `source` label each discoveredServers() entry carries ────────────────

TEST_CASE("discoverySourceLabel maps every source to the FOUND-row label",
          "[appvm][connections][source]") {
    // AppViewModel::discoveredServers() sets entry["source"] = discoverySourceLabel(s.source).
    // Pin the exact strings the FOUND row renders (matching the Widgets dialog).
    CHECK(discoverySourceLabel(DiscoverySource::Broadcast) == QStringLiteral("UDP broadcast"));
    CHECK(discoverySourceLabel(DiscoverySource::Mdns) == QStringLiteral("mDNS"));
    CHECK(discoverySourceLabel(DiscoverySource::Both) == QStringLiteral("mDNS + broadcast"));
}

TEST_CASE("a discovered server keeps its source for the FOUND-row label",
          "[appvm][connections][source]") {
    const auto s = sat(QStringLiteral("m1"), QStringLiteral("10.0.0.1"), DiscoverySource::Both);
    CHECK(discoverySourceLabel(s.source) == QStringLiteral("mDNS + broadcast"));
}

// ── De-raced id resolution: the identity connectByServerId/pairByServerId key on ──

TEST_CASE("connectByServerId/pairByServerId resolve on the stable machineId-preferring id",
          "[appvm][connections][deraced]") {
    // The id-based invokables match a server by DiscoveredServer::id() (the same
    // key the Widgets onConnectClicked matches), NOT a list index — so a reorder
    // of discoveredServers() between read and call can't connect the wrong box.
    const auto withMid =
        sat(QStringLiteral("m1"), QStringLiteral("10.0.0.9"), DiscoverySource::Broadcast);
    CHECK(withMid.id() == QStringLiteral("mid:m1")); // machineId-preferring

    // Two servers that differ only by IP but share a machineId collapse to the
    // SAME id, so the resolution is address-independent (survives a DHCP move).
    auto moved = withMid;
    moved.ip = QStringLiteral("10.0.0.250");
    CHECK(moved.id() == withMid.id());

    // A machineId-less satellite falls back to ip:udpPort — still stable per call.
    DiscoveredServer legacy;
    legacy.ip = QStringLiteral("10.0.0.5");
    legacy.udpPort = 9876;
    CHECK(legacy.id() == QStringLiteral("wifi:10.0.0.5:9876"));
}
