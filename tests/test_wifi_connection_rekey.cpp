// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The proactive re-key wiring (contract §Crypto: clients SHOULD re-PUT once
// the send counter crosses 0xF0000000): the alive tick must fire the manager's
// onRekey callback when reducer::counterNeedsRepush trips, exactly once per
// approach, and re-arm only after the re-key lands (counter back under the
// threshold). The tick is driven directly through the test seam — no timer
// waits.

#include "Network/SatelliteClient.h"
#include "Network/WifiConnection.h"
#include "Network/WinsockInit.h"
#include "core/reducer/Reconcile.h"
#include "satellite_client_test_access.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>

namespace dish::net {

// Definition of the test-only friend seam declared in WifiConnection.h.
class WifiConnectionTestAccess {
  public:
    static void tick(WifiConnection& conn) { conn.onAliveTick(); }
};

} // namespace dish::net

using dish::net::SatelliteClient;
using dish::net::SatelliteClientTestAccess;
using dish::net::WifiConnection;
using dish::net::WifiConnectionTestAccess;

namespace {

// The QTimer markConnected starts needs an application object; Catch2's main
// doesn't make one.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

SOCKET bindLoopback(std::uint16_t& port) {
    const SOCKET fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd == INVALID_SOCKET) { return INVALID_SOCKET; }
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = 0;
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    int len = static_cast<int>(sizeof(addr));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
        ::getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
        ::closesocket(fd);
        return INVALID_SOCKET;
    }
    port = ntohs(addr.sin_port);
    return fd;
}

std::array<std::uint8_t, 32> key(std::uint8_t fill) {
    std::array<std::uint8_t, 32> k{};
    k.fill(fill);
    return k;
}

dish::models::DiscoveredServer server() {
    dish::models::DiscoveredServer s;
    s.machineId = QStringLiteral("m1");
    s.ip = QStringLiteral("127.0.0.1");
    s.name = QStringLiteral("Sat");
    return s;
}

} // namespace

TEST_CASE("alive tick fires the rekey callback once per threshold approach and re-arms after "
          "landing",
          "[rekey]") {
    ensureApp();
    const dish::net::WinsockInit winsock;
    std::uint16_t port = 0;
    const SOCKET fd = bindLoopback(port);
    REQUIRE(fd != INVALID_SOCKET);

    auto client = std::make_shared<SatelliteClient>();
    REQUIRE(client->openSocket("127.0.0.1", port));
    client->setConnectionParams({0x11, 0x22, 0x33, 0x44}, key(0xA5));

    WifiConnection conn(WifiConnection::idFor(server()), server());
    int rekeyCalls = 0;
    conn.markConnecting();
    conn.markConnected(
        client, QStringLiteral("conn_1"), /*epoch=*/0, /*mouseControlGranted=*/false,
        /*onDead=*/[] {}, /*onClose=*/[](std::uint8_t) {}, /*onReconcile=*/[] {},
        /*onRekey=*/[&rekeyCalls] { rekeyCalls++; });

    // Below the threshold: no fire.
    WifiConnectionTestAccess::tick(conn);
    CHECK(rekeyCalls == 0);

    // Crossing 0xF0000000 fires the callback exactly once, not once per tick.
    SatelliteClientTestAccess::seedSendCounter(*client, dish::reducer::kCounterRepushThreshold);
    WifiConnectionTestAccess::tick(conn);
    CHECK(rekeyCalls == 1);
    WifiConnectionTestAccess::tick(conn);
    WifiConnectionTestAccess::tick(conn);
    CHECK(rekeyCalls == 1);

    // The re-key lands (fresh token/key, as the manager's rekey installs): the
    // counter restarts under the threshold and the latch re-arms without an
    // immediate re-fire.
    client->setConnectionParams({0x55, 0x66, 0x77, 0x88}, key(0x3C));
    CHECK_FALSE(dish::reducer::counterNeedsRepush(client->sendCounter()));
    WifiConnectionTestAccess::tick(conn);
    CHECK(rekeyCalls == 1);

    // The next approach to exhaustion fires again.
    SatelliteClientTestAccess::seedSendCounter(*client, dish::reducer::kCounterRepushThreshold);
    WifiConnectionTestAccess::tick(conn);
    CHECK(rekeyCalls == 2);

    conn.markDisconnected();
    ::closesocket(fd);
}

TEST_CASE("alive tick tolerates an absent rekey callback past the threshold", "[rekey]") {
    ensureApp();
    const dish::net::WinsockInit winsock;
    std::uint16_t port = 0;
    const SOCKET fd = bindLoopback(port);
    REQUIRE(fd != INVALID_SOCKET);

    auto client = std::make_shared<SatelliteClient>();
    REQUIRE(client->openSocket("127.0.0.1", port));
    client->setConnectionParams({0x11, 0x22, 0x33, 0x44}, key(0xA5));

    WifiConnection conn(QStringLiteral("mid:m2"), server());
    conn.markConnecting();
    conn.markConnected(client, QStringLiteral("conn_2"), /*epoch=*/0,
                       /*mouseControlGranted=*/false, {}, {}, {}, {});
    SatelliteClientTestAccess::seedSendCounter(*client, dish::reducer::kCounterRepushThreshold);
    WifiConnectionTestAccess::tick(conn); // must not crash
    conn.markDisconnected();
    ::closesocket(fd);
}
