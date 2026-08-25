// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Byte-exact encoder tests for the Moonlight control-stream codec, checked
// against the fixtures published in Wolf's input-data.adoc and testControl.cpp.

#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightCrypto.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace dish::moonlight;
namespace mc = dish::moonlight::crypto;

namespace {
std::string hx(const std::vector<std::uint8_t>& b) { return mc::hexEncode(b); }
} // namespace

TEST_CASE("MOUSE_MOVE_REL matches the input-data.adoc network fixture", "[moonlight][control]") {
    // dx=-1 (0xFFFF), dy=0 -> 06 02 0C 00 00 00 00 08 07 00 00 00 FF FF 00 00
    const auto bytes = encodeMouseMoveRel(-1, 0);
    REQUIRE(hx(bytes) == "06020C000000000807000000FFFF0000");
}

TEST_CASE("CONTROLLER_MULTI matches the Wolf joypad fixture (A pressed)", "[moonlight][control]") {
    // From testControl.cpp: controller 0, active mask 1, button A (0x1000).
    ControllerState s;
    s.controllerNumber = 0;
    s.activeGamepadMask = 0x0001;
    s.buttonFlags = static_cast<std::uint16_t>(kBtnA);
    const auto bytes = encodeControllerMulti(s);
    REQUIRE(bytes.size() == kControllerMultiBytes);
    REQUIRE(hx(bytes) ==
            "060222000000001E0C0000001A000000010014000010000000000000000000009C0000005500");
}

TEST_CASE("CONTROLLER_MULTI carries sticks, triggers and flags2", "[moonlight][control]") {
    ControllerState s;
    s.controllerNumber = 1;
    s.activeGamepadMask = 0x0003;
    s.buttonFlags = static_cast<std::uint16_t>(kBtnA | kBtnStart);
    s.buttonFlags2 = static_cast<std::uint16_t>(kBtnTouchpad >> 16);
    s.leftTrigger = 0x7F;
    s.rightTrigger = 0xFF;
    s.leftStickX = 0x1234;
    s.leftStickY = static_cast<std::int16_t>(0xFEDC);
    s.rightStickX = -1;
    s.rightStickY = 0x0100;

    // The fixed-buffer encoder and the vector convenience must agree byte for byte.
    std::uint8_t buf[kControllerMultiBytes];
    const std::size_t n = encodeControllerMulti(s, buf);
    REQUIRE(n == kControllerMultiBytes);
    const std::vector<std::uint8_t> viaBuf(buf, buf + n);
    REQUIRE(viaBuf == encodeControllerMulti(s));

    // Little-endian field placement: ctrl# at 14, active mask at 16, triggers
    // at 22/23.
    REQUIRE(viaBuf[14] == 0x01); // controllerNumber lo
    REQUIRE(viaBuf[16] == 0x03); // active mask lo
    REQUIRE(viaBuf[22] == 0x7F); // LT
    REQUIRE(viaBuf[23] == 0xFF); // RT
}

TEST_CASE("PERIODIC_PING and TERMINATION headers", "[moonlight][control]") {
    REQUIRE(hx(encodePeriodicPing()) == "00020000");
    // 0100 (type) 0400 (len=4) 80030023 (graceful reason, big-endian)
    REQUIRE(hx(encodeTermination()) == "0001040080030023");
}

TEST_CASE("encodeRtpPing carries the session payload, else the legacy form",
          "[moonlight][control][rtp]") {
    // With a 16-char X-SS-Ping-Payload: [payload 16][seq u32 LE] = 20 bytes.
    const std::string payload = "0123456789ABCDEF";
    const auto withPayload = encodeRtpPing(payload, 0x01020304);
    REQUIRE(withPayload.size() == 20);
    REQUIRE(std::string(withPayload.begin(), withPayload.begin() + 16) == payload);
    REQUIRE(withPayload[16] == 0x04); // little-endian seq
    REQUIRE(withPayload[17] == 0x03);
    REQUIRE(withPayload[18] == 0x02);
    REQUIRE(withPayload[19] == 0x01);

    // A host that sent no payload gets the 4-byte legacy ping.
    REQUIRE(hx(encodeRtpPing("", 7)) == "50494E47"); // "PING"
    // A wrong-length payload is not half-sent; it degrades to the legacy form.
    REQUIRE(encodeRtpPing("short", 0).size() == 4);
}

TEST_CASE("CONTROLLER_ARRIVAL announces type and caps", "[moonlight][control]") {
    const auto bytes = encodeControllerArrival(
        0, kPadTypePlayStation, kPadCapAnalogTriggers | kPadCapRumble | kPadCapGyro, 0xDEADBEEF);
    // wrapper(12) + body(7)
    REQUIRE(bytes.size() == 19);
    // input type is 0x55000004 little-endian at offset 8.
    REQUIRE(bytes[8] == 0x04);
    REQUIRE(bytes[9] == 0x00);
    REQUIRE(bytes[10] == 0x00);
    REQUIRE(bytes[11] == 0x55);
    REQUIRE(bytes[12] == 0x00);                // controllerNumber
    REQUIRE(bytes[13] == kPadTypePlayStation); // type
    REQUIRE(bytes[14] == (kPadCapAnalogTriggers | kPadCapRumble | kPadCapGyro));
}

TEST_CASE("decodeServerEvent parses each event body", "[moonlight][control][decode]") {
    // RUMBLE_DATA: type 0x010b, len 10, unused 4, ctrl=1, low=0x1122, high=0x3344
    {
        const auto p = *mc::hexDecode("0B010A0000000000010022114433");
        const auto ev = decodeServerEvent(p.data(), p.size());
        REQUIRE(ev.has_value());
        REQUIRE(ev->type == ServerEventType::Rumble);
        REQUIRE(ev->rumble.controllerNumber == 1);
        REQUIRE(ev->rumble.lowFreq == 0x1122);
        REQUIRE(ev->rumble.highFreq == 0x3344);
    }
    // RUMBLE_TRIGGERS: type 0x5500, len 6, ctrl=2, left=0x00AA, right=0x00BB
    {
        const auto p = *mc::hexDecode("005506000200AA00BB00");
        const auto ev = decodeServerEvent(p.data(), p.size());
        REQUIRE(ev.has_value());
        REQUIRE(ev->type == ServerEventType::RumbleTriggers);
        REQUIRE(ev->triggers.controllerNumber == 2);
        REQUIRE(ev->triggers.left == 0x00AA);
        REQUIRE(ev->triggers.right == 0x00BB);
    }
    // MOTION_EVENT: type 0x5501, len 5, ctrl=0, rate=100 (0x0064), type=gyro(2)
    {
        const auto p = *mc::hexDecode("015505000000640002");
        const auto ev = decodeServerEvent(p.data(), p.size());
        REQUIRE(ev.has_value());
        REQUIRE(ev->type == ServerEventType::MotionEvent);
        REQUIRE(ev->motion.controllerNumber == 0);
        REQUIRE(ev->motion.reportRateHz == 100);
        REQUIRE(ev->motion.motionType == kMotionGyro);
    }
    // RGB_LED: type 0x5502, len 5, ctrl=0, r=0x11 g=0x22 b=0x33
    {
        const auto p = *mc::hexDecode("025505000000112233");
        const auto ev = decodeServerEvent(p.data(), p.size());
        REQUIRE(ev.has_value());
        REQUIRE(ev->type == ServerEventType::RgbLed);
        REQUIRE(ev->led.controllerNumber == 0);
        REQUIRE(ev->led.r == 0x11);
        REQUIRE(ev->led.g == 0x22);
        REQUIRE(ev->led.b == 0x33);
    }
}

TEST_CASE("decodeServerEvent rejects short and unknown buffers", "[moonlight][control][decode]") {
    // Too short for even a header.
    const std::uint8_t tiny[2] = {0x0B, 0x01};
    REQUIRE_FALSE(decodeServerEvent(tiny, sizeof(tiny)).has_value());
    REQUIRE_FALSE(decodeServerEvent(nullptr, 0).has_value());

    // Unknown type decodes to Unknown, not nullopt (ignored gracefully).
    const auto unknown = *mc::hexDecode("99990000");
    const auto ev = decodeServerEvent(unknown.data(), unknown.size());
    REQUIRE(ev.has_value());
    REQUIRE(ev->type == ServerEventType::Unknown);

    // A known type with a truncated body degrades to Unknown, never a read past
    // the end.
    const auto shortBody = *mc::hexDecode("0B010A000000"); // claims len 10, has 2
    const auto ev2 = decodeServerEvent(shortBody.data(), shortBody.size());
    REQUIRE(ev2.has_value());
    REQUIRE(ev2->type == ServerEventType::Unknown);
}
