// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteClient session-level invariants that don't need a socket: the send
// counter starts at 1 (the protocol-1 fix to the pre-1 off-by-one), the
// enriched-ack / close-notify state resets on each setConnectionParams, and the
// heartbeat cadence/miss thresholds match the contract (2s / 2-miss
// not-responding / 5-miss dead).

#include "Network/SatelliteClient.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using dish::net::SatelliteClient;

namespace {
std::array<std::uint8_t, 4> token4() { return {0x00, 0x07, 0xA1, 0xB2}; }
std::array<std::uint8_t, 32> key32() {
    std::array<std::uint8_t, 32> k{};
    k[0] = 0xAB;
    return k;
}
} // namespace

TEST_CASE("send counter starts at 1 after setConnectionParams", "[satellite][counter]") {
    // The pre-protocol-1 client returned-then-incremented from 0, so its first
    // packet used counter 0 — off-spec. Protocol-1 starts at 1.
    SatelliteClient c;
    c.setConnectionParams(token4(), key32());
    REQUIRE(c.sendCounter() == 1u);
}

TEST_CASE("setConnectionParams resets reconcile/close state to -1", "[satellite][session]") {
    SatelliteClient c;
    c.setConnectionParams(token4(), key32());
    // No enriched ack / close-notify seen yet.
    REQUIRE(c.serverEpoch() == -1);
    REQUIRE(c.serverBitmap() == -1);
    REQUIRE(c.backendAvailable() == -1);
    REQUIRE(c.activeControllerCount() == -1);
    REQUIRE(c.sessionCloseReason() == -1);
    REQUIRE(c.isAlive());
}

TEST_CASE("a fresh client re-keyed twice keeps the counter at 1", "[satellite][counter]") {
    // Each session PUT rotates token/salt/key and restarts the counter — no
    // cross-session nonce reuse.
    SatelliteClient c;
    c.setConnectionParams(token4(), key32());
    c.setConnectionParams({0x11, 0x22, 0x33, 0x44}, key32());
    REQUIRE(c.sendCounter() == 1u);
}

TEST_CASE("heartbeat cadence + miss thresholds match the contract", "[satellite][heartbeat]") {
    REQUIRE(SatelliteClient::kHeartbeatIntervalMs == 2000);
    REQUIRE(SatelliteClient::kHeartbeatMissNotResponding == 2);
    REQUIRE(SatelliteClient::kHeartbeatMissMax == 5);
}

TEST_CASE("close + heartbeat handler installation is null-safe", "[satellite][session]") {
    // Installing handlers before any session is fine (they fire from the receive
    // thread once live); just exercise the setters don't crash without a socket.
    SatelliteClient c;
    c.setHeartbeatAckHandler([](const SatelliteClient::HeartbeatAck&) {});
    c.setCloseHandler([](std::uint8_t) {});
    c.setRumbleHandler([](const SatelliteClient::RumbleMessage&) {});
    c.setLightbarHandler([](const SatelliteClient::LightbarMessage&) {});
    SUCCEED();
}
