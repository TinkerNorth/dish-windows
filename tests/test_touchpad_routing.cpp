// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for the pure touchpad routing reducer in
// core/reducer/TouchpadRouting.h: assembleTouchpadForward (threads eventTimeMs
// end-to-end — the 2e routing fix) and the kTouchpadMouseControlV1 constant
// (D2: mouse mode off for v1). The on-screen TouchpadPadCoordinator /
// TouchpadState surface tests are phone-only SKIP; dish-windows forwards a real
// controller's touchpad, so this pins the physical-pad forward routing. The
// wire byte layout (16 bytes, eventTimeMs at bytes 12..15) is pinned separately
// by test_satellite_client_touchpad against the Wave 1 encoder.

#include "Network/SatelliteClient.h"
#include "core/reducer/TouchpadRouting.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using dish::reducer::assembleTouchpadForward;
using dish::reducer::kTouchpadMouseControlV1;
using dish::reducer::TouchpadForward;

namespace {

std::uint32_t readLe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

TEST_CASE("assembleTouchpadForward carries eventTimeMs (the 2e fix)", "[touchpad][routing]") {
    const auto f = assembleTouchpadForward(/*f0Active=*/true, /*f0Id=*/1, /*f0X=*/10, /*f0Y=*/20,
                                           /*f1Active=*/false, /*f1Id=*/0, /*f1X=*/0, /*f1Y=*/0,
                                           /*button=*/false, /*eventTimeMs=*/0x12345678U);
    REQUIRE(f.eventTimeMs == 0x12345678U);
}

TEST_CASE("assembleTouchpadForward preserves every finger/button field", "[touchpad][routing]") {
    const auto f = assembleTouchpadForward(true, 7, -100, 200, true, 9, 300, -400, true, 42U);
    REQUIRE(f.finger0Active);
    REQUIRE(f.finger0Id == 7U);
    REQUIRE(f.finger0X == -100);
    REQUIRE(f.finger0Y == 200);
    REQUIRE(f.finger1Active);
    REQUIRE(f.finger1Id == 9U);
    REQUIRE(f.finger1X == 300);
    REQUIRE(f.finger1Y == -400);
    REQUIRE(f.buttonPressed);
    REQUIRE(f.eventTimeMs == 42U);
}

TEST_CASE("assembleTouchpadForward does not zero an inactive finger's coords",
          "[touchpad][routing]") {
    // The active flags are the sole source of truth (matching the encoder's pure
    // layout contract) — an inactive finger's id/coords pass through verbatim.
    const auto f = assembleTouchpadForward(false, 0x11, 0x2222, 0x3333, false, 0x44, 0x5555, 0x6666,
                                           false, 0x77777777U);
    REQUIRE_FALSE(f.finger0Active);
    REQUIRE(f.finger0Id == 0x11U);
    REQUIRE(f.finger0X == 0x2222);
    REQUIRE_FALSE(f.finger1Active);
    REQUIRE(f.finger1Id == 0x44U);
    REQUIRE(f.finger1Y == 0x6666);
    REQUIRE(f.eventTimeMs == 0x77777777U);
}

TEST_CASE("assembled forward feeds the wire encoder with eventTimeMs intact",
          "[touchpad][routing]") {
    // End-to-end seam check: the routing's eventTimeMs reaches the encoder's
    // trailing u32 (bytes 12..15). A regression that dropped eventTimeMs would
    // re-introduce the server-side drop of a sub-16-byte payload.
    const auto f = assembleTouchpadForward(true, 2, 5, 6, false, 0, 0, 0, true, 0xCAFEBABEU);
    const auto wire = dish::net::SatelliteClient::encodeTouchpadPayload(
        /*ctrlIdx=*/0, f.finger0Active, f.finger0Id, f.finger0X, f.finger0Y, f.finger1Active,
        f.finger1Id, f.finger1X, f.finger1Y, f.buttonPressed, f.eventTimeMs);
    REQUIRE(wire.size() == 16U);
    REQUIRE(readLe32(&wire[12]) == 0xCAFEBABEU);
}

TEST_CASE("touchpad ships v1 with mouse control disabled (D2)", "[touchpad][routing]") {
    // No mouse mode in v1; the descriptor (sent by 2b) carries this value.
    REQUIRE(kTouchpadMouseControlV1 == false);
}

TEST_CASE("TouchpadForward equality compares all fields including eventTimeMs",
          "[touchpad][routing]") {
    const auto a = assembleTouchpadForward(true, 1, 2, 3, false, 0, 0, 0, false, 100U);
    auto b = a;
    REQUIRE(a == b);
    b.eventTimeMs = 101U;
    REQUIRE_FALSE(a == b);
}
