// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The _nvstream._tcp mapping + wire-parse helpers, tested without a socket.

#include "source/connection/NvstreamDiscovery.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace dish::net;

TEST_CASE("nvstreamServiceToHost fills fixed ports and falls back to IP", "[moonlight][nvstream]") {
    const auto named =
        nvstreamServiceToHost(QStringLiteral("living-room-pc"), QStringLiteral("192.168.1.50"));
    REQUIRE(named.has_value());
    REQUIRE(named->name == QStringLiteral("living-room-pc"));
    REQUIRE(named->ip == QStringLiteral("192.168.1.50"));
    REQUIRE(named->httpPort == dish::models::kMoonlightHttpPort);
    REQUIRE(named->httpsPort == dish::models::kMoonlightHttpsPort);
    REQUIRE(named->discovered);

    // Empty instance name -> the IP stands in.
    const auto anon = nvstreamServiceToHost(QString(), QStringLiteral("10.0.0.7"));
    REQUIRE(anon.has_value());
    REQUIRE(anon->name == QStringLiteral("10.0.0.7"));

    // No address -> nothing to connect to.
    REQUIRE_FALSE(nvstreamServiceToHost(QStringLiteral("x"), QString()).has_value());
}

TEST_CASE("parseResponse reads an A + SRV answer", "[moonlight][nvstream]") {
    // Header: id 0, flags 0x8400 (response), qd 0, an 2.
    std::vector<std::uint8_t> pkt = {0, 0, 0x84, 0x00, 0, 0, 0, 2, 0, 0, 0, 0};

    auto append = [&pkt](std::initializer_list<std::uint8_t> b) { pkt.insert(pkt.end(), b); };
    // Answer 1: A record, root name, IP 192.168.1.5.
    append({0x00});                   // name = root
    append({0x00, 0x01});             // type A
    append({0x80, 0x01});             // class IN + cache-flush
    append({0x00, 0x00, 0x00, 0x78}); // TTL
    append({0x00, 0x04});             // rdlen
    append({192, 168, 1, 5});         // IP
    // Answer 2: SRV record, root name, minimal rdata (prio, weight, port, root).
    append({0x00});                               // name = root
    append({0x00, 0x21});                         // type SRV
    append({0x80, 0x01});                         // class
    append({0x00, 0x00, 0x00, 0x78});             // TTL
    append({0x00, 0x07});                         // rdlen = 7
    append({0x00, 0x00, 0x00, 0x00, 0xBB, 0x35}); // prio, weight, port 47925
    append({0x00});                               // target = root

    const auto host = nvstream_detail::parseResponse(pkt.data(), pkt.size());
    REQUIRE(host.has_value());
    REQUIRE(host->ip == QStringLiteral("192.168.1.5"));
    REQUIRE(host->httpPort == dish::models::kMoonlightHttpPort);
}

TEST_CASE("parseResponse rejects a too-short or SRV-less packet", "[moonlight][nvstream]") {
    const std::uint8_t tiny[4] = {0, 0, 0, 0};
    REQUIRE_FALSE(nvstream_detail::parseResponse(tiny, sizeof(tiny)).has_value());

    // A record only, no SRV -> not a usable service.
    std::vector<std::uint8_t> pkt = {0, 0, 0x84, 0x00, 0, 0, 0, 1, 0, 0, 0, 0};
    pkt.insert(pkt.end(),
               {0x00, 0x00, 0x01, 0x80, 0x01, 0x00, 0x00, 0x00, 0x78, 0x00, 0x04, 192, 168, 1, 5});
    REQUIRE_FALSE(nvstream_detail::parseResponse(pkt.data(), pkt.size()).has_value());
}
