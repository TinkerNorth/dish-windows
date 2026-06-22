// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// NetworkUtilsTest (private-IP slice, PURE) — isPrivateHostLiteral. Port of the
// dish-android NetworkUtilsTest assertions for IpLiterals.isPrivateHostLiteral:
// RFC-1918 / link-local / loopback / unique-local literals are private; public
// addresses, near-misses, and hostnames are not. Landed once in core/net so 2a
// (discovery vetting) and 2b (public-IP connect refusal) share the rule.

#include "core/net/IpLiterals.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using dish::net::isPrivateHostLiteral;

TEST_CASE("isPrivateHostLiteral accepts private IPv4 / IPv6 literals", "[iplit]") {
    CHECK(isPrivateHostLiteral("10.0.0.5"));       // 10/8
    CHECK(isPrivateHostLiteral("172.16.0.1"));     // bottom of 172.16/12
    CHECK(isPrivateHostLiteral("172.31.255.255")); // top of 172.16/12
    CHECK(isPrivateHostLiteral("192.168.1.1"));    // 192.168/16
    CHECK(isPrivateHostLiteral("169.254.1.1"));    // 169.254/16 link-local
    CHECK(isPrivateHostLiteral("127.0.0.1"));      // 127/8 loopback
    CHECK(isPrivateHostLiteral("::1"));            // IPv6 loopback
    CHECK(isPrivateHostLiteral("fe80::1"));        // fe80::/10 link-local
    CHECK(isPrivateHostLiteral("[fe80::1]"));      // bracketed authority form
    CHECK(isPrivateHostLiteral("fc00::1"));        // fc00::/7 unique-local
}

TEST_CASE("isPrivateHostLiteral rejects public literals and hostnames", "[iplit]") {
    CHECK_FALSE(isPrivateHostLiteral("8.8.8.8"));     // public
    CHECK_FALSE(isPrivateHostLiteral("172.32.0.1"));  // just past 172.16/12
    CHECK_FALSE(isPrivateHostLiteral("11.0.0.1"));    // 11.x is public (only 10/8 private)
    CHECK_FALSE(isPrivateHostLiteral("172.15.0.1"));  // just below 172.16/12
    CHECK_FALSE(isPrivateHostLiteral("example.com")); // hostname, not a literal
    CHECK_FALSE(isPrivateHostLiteral(""));            // empty
    CHECK_FALSE(isPrivateHostLiteral("999.1.1.1"));   // octet out of range
    CHECK_FALSE(isPrivateHostLiteral("1.2.3"));       // too few octets
}

// Windows-specific extension: a few more IPv6 edges the parser must handle. The
// classifier looks only at the high bytes, so a v4-mapped address (::ffff:x) is
// NOT private regardless of the embedded v4 — it isn't in fc00::/7, fe80::/10,
// or ::1 — matching android's byte-range check.
TEST_CASE("isPrivateHostLiteral handles IPv6 edge forms", "[iplit]") {
    CHECK(isPrivateHostLiteral("fd12:3456:789a::1"));          // fd00::/8 (part of fc00::/7)
    CHECK_FALSE(isPrivateHostLiteral("2001:4860:4860::8888")); // public IPv6
    CHECK_FALSE(isPrivateHostLiteral("fe80::1%eth0"));         // zone id rejected
    CHECK_FALSE(isPrivateHostLiteral("::ffff:8.8.8.8"));  // v4-mapped, high bytes 0 -> not private
    CHECK_FALSE(isPrivateHostLiteral("::ffff:10.0.0.1")); // v4-mapped, high bytes 0 -> not private
}
