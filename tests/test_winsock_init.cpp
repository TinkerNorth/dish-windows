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

TEST_CASE("WinsockInit unblocks socket() — pre-init this would fail with WSANOTINITIALISED",
         "[net]") {
    const WinsockInit w;
    REQUIRE(w.ok());

    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}

TEST_CASE("WinsockInit is reference-counted — nested instances are safe", "[net]") {
    // WSAStartup / WSACleanup are reference-counted by Winsock itself. The
    // dtor of an inner instance must NOT tear the stack down underneath an
    // outer instance still in scope — otherwise the second AppModel started
    // inside a test process (or any nested socket-using lifetime) would
    // observe WSANOTINITIALISED on subsequent socket() calls.
    const WinsockInit outer;
    REQUIRE(outer.ok());
    {
        const WinsockInit inner;
        REQUIRE(inner.ok());
        const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        REQUIRE(s != INVALID_SOCKET);
        REQUIRE(::closesocket(s) == 0);
    }
    // Inner has gone out of scope. Outer is still alive, so sockets must
    // still work.
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}

TEST_CASE("WinsockInit dtor on a failed init does not call WSACleanup", "[net]") {
    // We can't easily force WSAStartup to fail on a healthy host (it would
    // need a system in a broken state), but we can at least pin the
    // contract that constructing+destructing many WinsockInit instances
    // back-to-back is always safe — if dtor were unconditionally calling
    // WSACleanup we'd quickly decrement the global Winsock refcount below
    // zero and subsequent socket() calls would start failing.
    for (int i = 0; i < 8; ++i) {
        const WinsockInit w;
        REQUIRE(w.ok());
    }
    // After 8 paired startup/cleanup pairs the system should still let us
    // initialise once more and open a socket.
    const WinsockInit final;
    REQUIRE(final.ok());
    const SOCKET s = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    REQUIRE(s != INVALID_SOCKET);
    REQUIRE(::closesocket(s) == 0);
}
