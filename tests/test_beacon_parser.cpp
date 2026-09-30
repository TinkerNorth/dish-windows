// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/ScopedSocket.h"
#include "Network/WinsockInit.h"
#include "source/connection/LANDiscovery.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QList>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <thread>

using dish::net::LANDiscovery;
using dish::net::ScopedSocket;

namespace {

// The scan listens this long; the sender repeats well inside it.
constexpr int kScanWindowMs = 600;
constexpr auto kResendInterval = std::chrono::milliseconds(25);

const QByteArray kTruncatedBeacon = QByteArrayLiteral(R"({"service":"satel)");
const QByteArray kBeacon =
    QByteArrayLiteral(R"({"service":"satellite","name":"office","udpPort":9876})");

sockaddr_in loopbackAt(std::uint16_t port) {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    return addr;
}

// Only the number is reserved: discover() does its own bind, so the probe socket is closed first.
std::uint16_t freeUdpPort() {
    const ScopedSocket probe;
    REQUIRE(probe.valid());
    sockaddr_in addr = loopbackAt(0);
    REQUIRE(::bind(probe.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    int length = static_cast<int>(sizeof(addr));
    REQUIRE(::getsockname(probe.get(), reinterpret_cast<sockaddr*>(&addr), &length) == 0);
    return ntohs(addr.sin_port);
}

// A satellite whose address also sends something that is not a beacon: a truncated beacon, then a
// whole one, over and over until it goes out of scope. The junk always leads, and the pair repeats
// because the scan binds its port at a moment this thread cannot see.
class JunkThenBeacon {
  public:
    explicit JunkThenBeacon(std::uint16_t port) : thread_(&JunkThenBeacon::run, this, port) {}
    ~JunkThenBeacon() {
        stop_.store(true);
        thread_.join();
    }
    JunkThenBeacon(const JunkThenBeacon&) = delete;
    JunkThenBeacon& operator=(const JunkThenBeacon&) = delete;
    JunkThenBeacon(JunkThenBeacon&&) = delete;
    JunkThenBeacon& operator=(JunkThenBeacon&&) = delete;

  private:
    void run(std::uint16_t port) const {
        const ScopedSocket sock;
        if (!sock.valid()) { return; }
        const sockaddr_in to = loopbackAt(port);
        while (!stop_.load()) {
            send(sock.get(), to, kTruncatedBeacon);
            send(sock.get(), to, kBeacon);
            std::this_thread::sleep_for(kResendInterval);
        }
    }

    static void send(SOCKET sock, const sockaddr_in& to, const QByteArray& datagram) {
        ::sendto(sock, datagram.constData(), static_cast<int>(datagram.size()), 0,
                 reinterpret_cast<const sockaddr*>(&to), static_cast<int>(sizeof(to)));
    }

    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// One whole scan of `port` while the satellite above is sending to it.
QList<dish::models::DiscoveredServer> scanBesideJunk(std::uint16_t port) {
    const JunkThenBeacon satellite(port);
    return LANDiscovery::discover(port, kScanWindowMs);
}

} // namespace

TEST_CASE("parseBeacon accepts a valid satellite beacon", "[discovery]") {
    const QString payload = QStringLiteral(
        R"({"service":"satellite","name":"office","udpPort":9876,"pairPort":9878,"httpPort":9877})");
    const auto s = LANDiscovery::parseBeacon(payload, "10.0.0.1");
    REQUIRE(s.has_value());
    REQUIRE(s->name == "office");
    REQUIRE(s->ip == "10.0.0.1");
    REQUIRE(s->udpPort == 9876);
    REQUIRE(s->pairPort == 9878);
    REQUIRE(s->httpPort == 9877);
}

TEST_CASE("parseBeacon rejects payloads from other services", "[discovery]") {
    const QString payload = QStringLiteral(R"({"service":"chromecast","name":"foo"})");
    REQUIRE_FALSE(LANDiscovery::parseBeacon(payload, "10.0.0.1").has_value());
}

TEST_CASE("parseBeacon rejects malformed JSON", "[discovery]") {
    REQUIRE_FALSE(LANDiscovery::parseBeacon("not json", "10.0.0.1").has_value());
    REQUIRE_FALSE(
        LANDiscovery::parseBeacon(QStringLiteral(R"({"service":"satellite",)"), "10.0.0.1")
            .has_value());
}

TEST_CASE("parseBeacon rejects beacons with an empty name", "[discovery]") {
    const QString payload = QStringLiteral(R"({"service":"satellite","name":""})");
    REQUIRE_FALSE(LANDiscovery::parseBeacon(payload, "10.0.0.1").has_value());
}

TEST_CASE("parseBeacon overrides any beacon-supplied ip with the observed source", "[discovery]") {
    const QString payload =
        QStringLiteral(R"({"service":"satellite","name":"office","ip":"1.1.1.1"})");
    const auto s = LANDiscovery::parseBeacon(payload, "10.0.0.7");
    REQUIRE(s.has_value());
    REQUIRE(s->ip == "10.0.0.7");
}

TEST_CASE("a stray datagram does not hide a later beacon from the same host", "[discovery]") {
    const dish::net::WinsockInit winsock;
    REQUIRE(winsock.ok());

    const auto found = scanBesideJunk(freeUdpPort());

    // One row, not one per repeat: the beacon is still deduplicated by address.
    REQUIRE(found.size() == 1);
    CHECK(found.front().name == "office");
    CHECK(found.front().ip == "127.0.0.1");
    CHECK(found.front().udpPort == 9876);
}
