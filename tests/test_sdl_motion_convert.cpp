// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

// Coverage for the SDL → wire conversion helpers in SdlMotionConvert.{h,cpp}.
// These translate SDL2's physical sensor units (gyro rad/s, accel m/s²,
// touchpad 0..1) into the int16 wire values defined by
// satellite/src/core/types.h. The conversion factors are a hard contract with
// the satellite receiver, so the full-scale anchor points are pinned here.
// The helpers used to live in an anonymous namespace inside
// SDLGamepadBridge.cpp; they were lifted into their own translation unit
// purely so this test could reach them without bringing up SDL.

#include "Input/SdlMotionConvert.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <cstdlib>

using dish::input::accelMps2ToInt16;
using dish::input::gyroRadPerSecToInt16;
using dish::input::touchpadCoordToInt16;

namespace {

// Helper: assert two int16s are within `tol` LSB of each other. The
// conversions run in single-precision float, so a full-scale value can land
// ±1 LSB off the ideal double-precision result; ±2 is a comfortable margin.
bool near16(std::int16_t actual, std::int16_t expected, int tol = 2) {
    return std::abs(static_cast<int>(actual) - static_cast<int>(expected)) <= tol;
}

} // namespace

// ── Gyro: rad/s → int16, full scale ±2000 deg/s ─────────────────────────────

TEST_CASE("gyroRadPerSecToInt16 maps +2000 deg/s to +full scale", "[motionconvert]") {
    // 34.9066 rad/s == 2000 deg/s == the positive full-scale anchor.
    REQUIRE(near16(gyroRadPerSecToInt16(34.9066f), 32767));
}

TEST_CASE("gyroRadPerSecToInt16 maps -2000 deg/s to -full scale", "[motionconvert]") {
    // -2000 deg/s lands on -32767 (not -32768) — the scale is symmetric
    // about zero with 32767 LSB per 2000 deg/s.
    REQUIRE(near16(gyroRadPerSecToInt16(-34.9066f), -32767));
}

TEST_CASE("gyroRadPerSecToInt16 maps zero to zero exactly", "[motionconvert]") {
    REQUIRE(gyroRadPerSecToInt16(0.0f) == 0);
}

TEST_CASE("gyroRadPerSecToInt16 clamps beyond +/-2000 deg/s", "[motionconvert]") {
    // Anything past the full-scale range saturates rather than wrapping.
    REQUIRE(gyroRadPerSecToInt16(100.0f) == 32767);
    REQUIRE(gyroRadPerSecToInt16(-100.0f) == -32768);
    // Just past the +full-scale anchor still saturates at the int16 max.
    REQUIRE(gyroRadPerSecToInt16(40.0f) == 32767);
    REQUIRE(gyroRadPerSecToInt16(-40.0f) == -32768);
}

// ── Accel: m/s² → int16, full scale ±4 g ────────────────────────────────────

TEST_CASE("accelMps2ToInt16 maps +1 g to ~8192", "[motionconvert]") {
    // 9.80665 m/s² == 1 g; with 32767 LSB per 4 g that is 32767/4 ≈ 8192.
    REQUIRE(near16(accelMps2ToInt16(9.80665f), 8192));
}

TEST_CASE("accelMps2ToInt16 maps +4 g to +full scale", "[motionconvert]") {
    // 39.2266 m/s² == 4 g == the positive full-scale anchor.
    REQUIRE(near16(accelMps2ToInt16(39.2266f), 32767));
}

TEST_CASE("accelMps2ToInt16 maps zero to zero exactly", "[motionconvert]") {
    REQUIRE(accelMps2ToInt16(0.0f) == 0);
}

TEST_CASE("accelMps2ToInt16 clamps beyond +/-4 g", "[motionconvert]") {
    REQUIRE(accelMps2ToInt16(100.0f) == 32767);
    REQUIRE(accelMps2ToInt16(-100.0f) == -32768);
    // Just past the 4 g anchor saturates.
    REQUIRE(accelMps2ToInt16(50.0f) == 32767);
    REQUIRE(accelMps2ToInt16(-50.0f) == -32768);
}

TEST_CASE("accelMps2ToInt16 keeps sign symmetry around -1 g", "[motionconvert]") {
    REQUIRE(near16(accelMps2ToInt16(-9.80665f), -8192));
}

// ── Touchpad: 0..1 normalised coord → int16 spanning the pad ────────────────

TEST_CASE("touchpadCoordToInt16 maps the 0..1 span to the full int16 range",
          "[motionconvert]") {
    REQUIRE(touchpadCoordToInt16(0.0f) == -32768);
    REQUIRE(touchpadCoordToInt16(1.0f) == 32767);
    // Mid-pad sits at the origin.
    REQUIRE(near16(touchpadCoordToInt16(0.5f), 0));
}

TEST_CASE("touchpadCoordToInt16 clamps out-of-range coordinates", "[motionconvert]") {
    // SDL should always report 0..1, but a value outside that span must
    // saturate at the pad edge rather than wrap.
    REQUIRE(touchpadCoordToInt16(-0.5f) == -32768);
    REQUIRE(touchpadCoordToInt16(2.0f) == 32767);
}
