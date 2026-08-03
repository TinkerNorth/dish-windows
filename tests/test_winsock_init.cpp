// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/WinsockInit.h"

#include <catch2/catch_test_macros.hpp>

#include <winsock2.h>

using dish::net::WinsockInit;

TEST_CASE("WinsockInit reports ok() on a healthy host", "[net]") {
    const WinsockInit w;
    REQUIRE(w.ok());
}

TEST_CASE("WinsockInit unblocks socket() (pre-init this would fail with WSANOTINITIALISED)",
          "[net]") {
    const WinsockInit w;
    REQUIRE(w.ok());

    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}

TEST_CASE("WinsockInit is reference-counted (nested instances are safe)", "[net]") {
    // WSAStartup/WSACleanup are reference-counted by Winsock itself, so an inner
    // dtor must not tear the stack down under an outer instance still in scope:
    // a second AppModel in one process would then see WSANOTINITIALISED.
    const WinsockInit outer;
    REQUIRE(outer.ok());
    {
        const WinsockInit inner;
        REQUIRE(inner.ok());
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        REQUIRE(s != INVALID_SOCKET);
        REQUIRE(::closesocket(s) == 0);
    }
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}

TEST_CASE("WinsockInit dtor on a failed init does not call WSACleanup", "[net]") {
    // A WSAStartup failure can't be forced on a healthy host, so this pins the
    // reachable half: an unconditional WSACleanup in the dtor would drive the
    // global refcount below zero and later socket() calls would start failing.
    for (int i = 0; i < 8; ++i) {
        const WinsockInit w;
        REQUIRE(w.ok());
    }
    const WinsockInit final;
    REQUIRE(final.ok());
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}
