// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

// Coverage for SatelliteClient::encodeMotionPayload / encodeBatteryPayload —
// the pure encoders for MSG_MOTION (0x000A) and MSG_BATTERY (0x000B). The
// wire layout these produce must match satellite/src/core/types.h::MotionReport
// and BatteryReport byte-for-byte. The receiver decodes the motion payload
// with decodeMotionReport() — explicit little-endian byte-shifts, NOT a
// struct memcpy — so the wire is byte-order- and struct-layout-independent.
// Same pattern as test_satellite_client_rumble.cpp — the encoders are
// public + static so we can pin the byte order without driving a live socket.

#include "Network/SatelliteClient.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using dish::net::SatelliteClient;

namespace {

// Helper: pull a host-LE int16 back out of a byte buffer for assertions.
std::int16_t readLe16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0]) |
                                     (static_cast<std::uint16_t>(p[1]) << 8));
}

std::uint32_t readLe32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

} // namespace

TEST_CASE("encodeMotionPayload places ctrlIdx at byte 0", "[motion]") {
    const auto out = SatelliteClient::encodeMotionPayload(/*ctrlIdx=*/7, 0, 0, 0, 0, 0, 0, 0);
    REQUIRE(out.size() == 17U);
    REQUIRE(out[0] == 7U);
}

TEST_CASE("encodeMotionPayload writes gyro / accel as LE int16", "[motion]") {
    const auto out = SatelliteClient::encodeMotionPayload(
        /*ctrlIdx=*/0, /*gx=*/0x0102, /*gy=*/0x0304, /*gz=*/0x0506,
        /*ax=*/0x0708, /*ay=*/0x090A, /*az=*/0x0B0C, /*dtUs=*/0);

    REQUIRE(readLe16(&out[1]) == 0x0102);
    REQUIRE(readLe16(&out[3]) == 0x0304);
    REQUIRE(readLe16(&out[5]) == 0x0506);
    REQUIRE(readLe16(&out[7]) == 0x0708);
    REQUIRE(readLe16(&out[9]) == 0x090A);
    REQUIRE(readLe16(&out[11]) == 0x0B0C);
}

TEST_CASE("encodeMotionPayload writes timestampDeltaUs as LE uint32", "[motion]") {
    const auto out =
        SatelliteClient::encodeMotionPayload(0, 0, 0, 0, 0, 0, 0, /*dtUs=*/0xDEADBEEFU);
    REQUIRE(readLe32(&out[13]) == 0xDEADBEEFU);
}

TEST_CASE("encodeMotionPayload handles full int16 range without overflow", "[motion]") {
    const auto out =
        SatelliteClient::encodeMotionPayload(0xFF, /*gx=*/-32768, /*gy=*/32767, /*gz=*/0,
                                             /*ax=*/-32768, /*ay=*/32767, /*az=*/-1, /*dtUs=*/0);
    REQUIRE(out[0] == 0xFFU);
    REQUIRE(readLe16(&out[1]) == -32768);
    REQUIRE(readLe16(&out[3]) == 32767);
    REQUIRE(readLe16(&out[7]) == -32768);
    REQUIRE(readLe16(&out[9]) == 32767);
    REQUIRE(readLe16(&out[11]) == -1);
}

TEST_CASE("encodeMotionPayload handles uint32 max delta", "[motion]") {
    const auto out = SatelliteClient::encodeMotionPayload(0, 0, 0, 0, 0, 0, 0, 0xFFFFFFFFU);
    REQUIRE(readLe32(&out[13]) == 0xFFFFFFFFU);
}

TEST_CASE("encodeMotionPayload zero-delta encodes as four zero bytes", "[motion]") {
    // First-packet sentinel — receiver expects exactly 0 (not 0xFFFFFFFF
    // or any other "no value" marker) so the inter-arrival timer can
    // recognise the start of a session.
    const auto out = SatelliteClient::encodeMotionPayload(0, 0, 0, 0, 0, 0, 0, 0);
    REQUIRE(out[13] == 0);
    REQUIRE(out[14] == 0);
    REQUIRE(out[15] == 0);
    REQUIRE(out[16] == 0);
}

TEST_CASE("encodeBatteryPayload writes ctrlIdx + level + status in order", "[battery]") {
    const auto out = SatelliteClient::encodeBatteryPayload(/*ctrlIdx=*/5, /*level=*/77,
                                                           /*status=*/2);
    REQUIRE(out.size() == 3U);
    REQUIRE(out[0] == 5U);
    REQUIRE(out[1] == 77U);
    REQUIRE(out[2] == 2U);
}

TEST_CASE("encodeBatteryPayload preserves 0xFF (unknown) sentinel", "[battery]") {
    const auto out = SatelliteClient::encodeBatteryPayload(0, 0xFF, 0);
    REQUIRE(out[1] == 0xFFU);
    REQUIRE(out[2] == 0U);
}

TEST_CASE("MSG constants match the wire-protocol spec (types.h)", "[constants]") {
    // These literal values are the contract between dish-windows and the
    // satellite receiver — bumping them silently would silently break
    // every paired client until they re-sync.
    REQUIRE(SatelliteClient::kMsgMotion == 0x000A);
    REQUIRE(SatelliteClient::kMsgBattery == 0x000B);
    REQUIRE(SatelliteClient::kCapMotion == 0x0004);
    REQUIRE(SatelliteClient::kBatteryLevelUnknown == 0xFF);
    REQUIRE(SatelliteClient::kBatteryStatusUnknown == 0);
    REQUIRE(SatelliteClient::kBatteryStatusDischarging == 1);
    REQUIRE(SatelliteClient::kBatteryStatusCharging == 2);
    REQUIRE(SatelliteClient::kBatteryStatusFull == 3);
    REQUIRE(SatelliteClient::kBatteryStatusWired == 4);
}

// ---------------------------------------------------------------------------
// CAP_MOTION — the per-controller capability bit advertised in
// MSG_CONTROLLER_ADD only when the bound pad actually has an IMU. An Xbox pad
// has no gyro/accelerometer and must NOT advertise it; the receiver still
// accepts best-effort motion either way, but the bit drives the web UI's
// motionCapable flag. Mirrors the withLightbarCapability tests.
// ---------------------------------------------------------------------------

TEST_CASE("withMotionCapability sets bit 0x0004 iff the pad has an IMU", "[motion][caps]") {
    // The static base word dish advertises today: analog triggers | rumble.
    const std::uint16_t base = SatelliteClient::kCapAnalogTriggers | SatelliteClient::kCapRumble;
    REQUIRE(base == 0x0003);

    SECTION("controller has an IMU -> CAP_MOTION is OR-ed in") {
        const std::uint16_t caps = SatelliteClient::withMotionCapability(base, true);
        REQUIRE(caps == 0x0007);
        REQUIRE((caps & SatelliteClient::kCapMotion) != 0);
    }
    SECTION("controller has no IMU -> word is unchanged") {
        const std::uint16_t caps = SatelliteClient::withMotionCapability(base, false);
        REQUIRE(caps == 0x0003);
        REQUIRE((caps & SatelliteClient::kCapMotion) == 0);
    }
}

TEST_CASE("withMotionCapability leaves the other capability bits intact", "[motion][caps]") {
    // Whatever the base word is, CAP_MOTION must only ever add bit 0x0004 —
    // never clear or change another bit.
    for (std::uint16_t base : {std::uint16_t{0x0000}, std::uint16_t{0x0003}, std::uint16_t{0x0008},
                               std::uint16_t{0xFFFB}}) {
        const std::uint16_t with = SatelliteClient::withMotionCapability(base, true);
        const std::uint16_t without = SatelliteClient::withMotionCapability(base, false);
        // "without" is a pure identity.
        REQUIRE(without == base);
        // "with" differs from the base only in bit 0x0004.
        REQUIRE((with & ~static_cast<std::uint16_t>(0x0004)) == base);
        REQUIRE((with | static_cast<std::uint16_t>(0x0004)) == with);
    }
}

TEST_CASE("withMotionCapability is idempotent when the bit is already set", "[motion][caps]") {
    const std::uint16_t base = 0x0003 | SatelliteClient::kCapMotion;
    REQUIRE(SatelliteClient::withMotionCapability(base, true) == base);
    REQUIRE(SatelliteClient::withMotionCapability(base, false) == base);
}

TEST_CASE("withMotionCapability and withLightbarCapability compose independently",
          "[motion][caps]") {
    // registerController folds both bits; the two must be orthogonal so an
    // IMU pad without an LED gets exactly CAP_MOTION and vice versa.
    const std::uint16_t base = SatelliteClient::kCapAnalogTriggers | SatelliteClient::kCapRumble;
    SECTION("IMU only") {
        const std::uint16_t caps = SatelliteClient::withLightbarCapability(
            SatelliteClient::withMotionCapability(base, true), false);
        REQUIRE(caps == 0x0007); // 0x0003 | CAP_MOTION
    }
    SECTION("LED only") {
        const std::uint16_t caps = SatelliteClient::withLightbarCapability(
            SatelliteClient::withMotionCapability(base, false), true);
        REQUIRE(caps == 0x000B); // 0x0003 | CAP_LIGHTBAR
    }
    SECTION("both (DualSense)") {
        const std::uint16_t caps = SatelliteClient::withLightbarCapability(
            SatelliteClient::withMotionCapability(base, true), true);
        REQUIRE(caps == 0x000F); // 0x0003 | CAP_MOTION | CAP_LIGHTBAR
    }
}

// ---------------------------------------------------------------------------
// MSG_CONTROLLER_CAPS_UPDATE (0x000E) and the motion-flag ACK bits — the wire
// extensions introduced in satellite PR #34. Pinning the literal values here
// keeps the contract honest: the receiver only recognises these exact bytes,
// so silently bumping them would silently break paired dish-windows clients.
// ---------------------------------------------------------------------------

TEST_CASE("MSG_CONTROLLER_CAPS_UPDATE constant pins the wire value", "[caps][constants]") {
    REQUIRE(SatelliteClient::kMsgControllerCapsUpdate == 0x000E);
    // Must not collide with any of the existing message types.
    REQUIRE(SatelliteClient::kMsgControllerCapsUpdate != SatelliteClient::kMsgControllerAdd);
    REQUIRE(SatelliteClient::kMsgControllerCapsUpdate != SatelliteClient::kMsgControllerType);
    REQUIRE(SatelliteClient::kMsgControllerCapsUpdate != SatelliteClient::kMsgLightbar);
}

TEST_CASE("ACK_MOTION_FLAG constants pin the wire values", "[motion][ack][constants]") {
    // Bit 0 = sink supported for type (PS yes, Xbox no on every shipping
    // backend); Bit 1 = backend OK (per-serial IMU node accepted). These are
    // the exact values satellite/src/core/types.h emits — a future receiver
    // can layer new flags on bits 2..7 without breaking us.
    REQUIRE(SatelliteClient::kAckMotionFlagSinkSupportedForType == 0x01);
    REQUIRE(SatelliteClient::kAckMotionFlagBackendOk == 0x02);
    REQUIRE((SatelliteClient::kAckMotionFlagSinkSupportedForType &
             SatelliteClient::kAckMotionFlagBackendOk) == 0);
}
