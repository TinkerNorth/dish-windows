// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Byte-exact encoder tests for the Moonlight control-stream codec, checked
// against the fixtures published in Wolf's input-data.adoc and testControl.cpp.

#include "core/input/GamepadButtonLayouts.h"
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

TEST_CASE("CONTROLLER_TOUCH lays out the header, pointer id and three netfloats",
          "[moonlight][control]") {
    // control.hpp CONTROLLER_TOUCH_PACKET: ctrl(1) event(1) zero(2) pointer(4)
    // x/y/pressure as little-endian floats. The INPUT_DATA wrapper's data-size
    // field is BIG-endian while everything else is little, which is the one
    // trap in this packet and the reason the whole 32 bytes are pinned here
    // rather than just the body.
    const auto bytes = encodeControllerTouch(/*ctrl=*/1, kTouchEventDown, /*pointerId=*/2,
                                             /*x=*/0.0F, /*y=*/1.0F, /*pressure=*/1.0F);
    REQUIRE(bytes.size() == 32U);
    // 0206 (INPUT_DATA) | 1C00 (len 28 LE) | 00000018 (data size 24 BE)
    // | 05000055 (0x55000005 LE) | 01 01 0000 | 02000000
    // | 00000000 (0.0f) | 0000803F (1.0f) | 0000803F (1.0f)
    REQUIRE(hx(bytes) == "0602"
                         "1C00"
                         "00000018"
                         "05000055"
                         "01"
                         "01"
                         "0000"
                         "02000000"
                         "00000000"
                         "0000803F"
                         "0000803F");
}

TEST_CASE("CONTROLLER_TOUCH carries the event type it was handed", "[moonlight][control]") {
    // The host holds a contact open until an UP arrives, so the event byte is
    // the difference between a released finger and a stranded one.
    // Body starts after the 12-byte INPUT_DATA wrapper: ctrl at 12, event at 13.
    CHECK(encodeControllerTouch(0, kTouchEventDown, 0, 0.0F, 0.0F, 1.0F)[13] == kTouchEventDown);
    CHECK(encodeControllerTouch(0, kTouchEventMove, 0, 0.0F, 0.0F, 1.0F)[13] == kTouchEventMove);
    CHECK(encodeControllerTouch(0, kTouchEventUp, 0, 0.0F, 0.0F, 0.0F)[13] == kTouchEventUp);
}

TEST_CASE("CONTROLLER_TOUCH pointer ids are a full little-endian u32", "[moonlight][control]") {
    // A pad's tracking id is one byte, but the wire field is four: truncating
    // it would collide two contacts on any host that keyed on the full value.
    const auto bytes = encodeControllerTouch(0, kTouchEventDown, 0x12345678, 0.0F, 0.0F, 1.0F);
    CHECK(bytes[16] == 0x78);
    CHECK(bytes[17] == 0x56);
    CHECK(bytes[18] == 0x34);
    CHECK(bytes[19] == 0x12);
}

TEST_CASE("CONTROLLER_TOUCH is the same length whatever it carries", "[moonlight][control]") {
    // Fixed-size packet: a host reads the body by offset, so a size that varied
    // with the payload would shift every field after it.
    CHECK(encodeControllerTouch(0, kTouchEventUp, 0, -1.0F, 2.0F, 0.0F).size() == 32U);
    CHECK(encodeControllerTouch(3, kTouchEventMove, 0xFFFFFFFF, 0.5F, 0.5F, 1.0F).size() == 32U);
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
    // A wrong-length payload is still the session form, padded or cut to the
    // 16 bytes it has room for: the host that handed it out matches by it, and
    // would answer the legacy form by never learning our media address.
    const auto padded = encodeRtpPing("short", 0);
    REQUIRE(padded.size() == 20);
    REQUIRE(std::string(padded.begin(), padded.begin() + 5) == "short");
    REQUIRE(padded[5] == 0);
    REQUIRE(encodeRtpPing(std::string(17, 'a'), 0).size() == 20);
}

TEST_CASE("SS_PING is the payload verbatim, never hex-decoded", "[moonlight][control][rtp]") {
    // A live Sunshine host's X-SS-Ping-Payload, off the wire. It LOOKS like hex
    // and is not: the host mints 16 printable ASCII characters and matches the
    // same 16 bytes coming back. Hex-decoding it makes an 8-byte datagram and
    // sending the text with no sequence makes a 16-byte one; both land in the
    // 5..19 byte dead zone Wolf's listener drops without a word, and the host
    // then reports `Initial Ping Timeout` ten seconds after PLAY.
    const std::string payload = "988E4FC7E070A22F";
    REQUIRE(payload.size() == 16);
    const auto ping = encodeRtpPing(payload, 0);
    // Exactly the 20 bytes the host logged back to us: the ASCII payload
    // followed by a little-endian u32 sequence of zero.
    REQUIRE(hx(ping) == "3938384534464337453037304132324600000000");
    REQUIRE(ping.size() == 20);

    // The sequence advances in place; nothing else in the datagram moves.
    const auto second = encodeRtpPing(payload, 1);
    REQUIRE(std::vector<std::uint8_t>(ping.begin(), ping.begin() + 16) ==
            std::vector<std::uint8_t>(second.begin(), second.begin() + 16));
    REQUIRE(hx(second).substr(32) == "01000000");

    // Nothing this encoder produces can land in the silently-dropped dead zone.
    for (const std::string& candidate :
         {std::string(), std::string("x"), std::string("short"), std::string(15, 'a'),
          std::string(16, 'a'), std::string(17, 'a')}) {
        const auto size = encodeRtpPing(candidate, 0).size();
        REQUIRE((size == 4 || size >= 20));
    }
}

TEST_CASE("CONTROLLER_ARRIVAL is read from a naturally aligned struct",
          "[moonlight][control][arrival]") {
    const auto bytes = encodeControllerArrival(
        0, kPadTypePlayStation, kPadCapAnalogTriggers | kPadCapRumble | kPadCapGyro, 0xDEADBEEF);
    // wrapper(12) + body(8). EIGHT, not seven: the u32 button mask starts at
    // offset 4 of the body, so offset 3 is the struct's alignment padding.
    REQUIRE(bytes.size() == 12 + kControllerArrivalBody);
    REQUIRE(bytes.size() == 20);
    // input type is 0x55000004 little-endian at offset 8.
    REQUIRE(bytes[8] == 0x04);
    REQUIRE(bytes[9] == 0x00);
    REQUIRE(bytes[10] == 0x00);
    REQUIRE(bytes[11] == 0x55);
    REQUIRE(bytes[12] == 0x00);                // controllerNumber
    REQUIRE(bytes[13] == kPadTypePlayStation); // type
    REQUIRE(bytes[14] == (kPadCapAnalogTriggers | kPadCapRumble | kPadCapGyro));
    REQUIRE(bytes[15] == 0x00); // the reserved pad byte
    // supportedButtons, little-endian, starting at the aligned offset.
    REQUIRE(bytes[16] == 0xEF);
    REQUIRE(bytes[17] == 0xBE);
    REQUIRE(bytes[18] == 0xAD);
    REQUIRE(bytes[19] == 0xDE);

    // The declared lengths follow the body: packet_len = 8 + body, and the
    // big-endian input size = 4 + body.
    REQUIRE(bytes[2] == 0x10); // packet_len 16, little-endian
    REQUIRE(bytes[3] == 0x00);
    REQUIRE(hx(std::vector<std::uint8_t>(bytes.begin() + 4, bytes.begin() + 8)) == "0000000C");
}

TEST_CASE("CONTROLLER_ARRIVAL carries the capability and button words a host reads back",
          "[moonlight][control][arrival]") {
    // The alignment tell, byte for byte. A live Sunshine host logged
    // `capabilities [FF03] supportedButtonFlags [000000FF]` for the seven-byte
    // body and `capabilities [0003] supportedButtonFlags [0000FFFF]` for this
    // one: analog triggers plus rumble, and the whole low sixteen buttons.
    const auto bytes =
        encodeControllerArrival(0, kPadTypeXbox, kPadCapAnalogTriggers | kPadCapRumble, 0x0000FFFF);
    REQUIRE(hx(bytes) == "060210000000000C0400005500010300FFFF0000");

    // Read back the way the host does, off the aligned offsets.
    REQUIRE(bytes[14] == 0x03);
    const std::uint32_t buttons = static_cast<std::uint32_t>(bytes[16]) |
                                  (static_cast<std::uint32_t>(bytes[17]) << 8) |
                                  (static_cast<std::uint32_t>(bytes[18]) << 16) |
                                  (static_cast<std::uint32_t>(bytes[19]) << 24);
    REQUIRE(buttons == 0x0000FFFFu);

    // A second pad announces under its own number, nothing else shifting.
    const auto second = encodeControllerArrival(1, kPadTypeNintendo, kPadCapRumble, 0x0000FFFF);
    REQUIRE(second[12] == 0x01);
    REQUIRE(second[13] == kPadTypeNintendo);
    REQUIRE(second[14] == kPadCapRumble);
    REQUIRE(second[15] == 0x00);
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

TEST_CASE("the Satellite-only mic-mute bit never reaches a Moonlight button word",
          "[moonlight][control]") {
    // XUSB and Moonlight button flags are bit-for-bit EXCEPT 0x0800, which
    // protocol 2 spends on the DualSense mic-mute STATE. A Direct-claimed
    // DualSense folds it into every report while muted, so the fold into
    // buttonFlags must strip it and touch nothing else.
    using dish::moonlight::sanitizeButtonFlags;
    CHECK(sanitizeButtonFlags(0x0800) == 0x0000);
    CHECK(sanitizeButtonFlags(0xFFFF) == 0xF7FF);
    CHECK(sanitizeButtonFlags(0x0000) == 0x0000);
    // Every assigned XUSB bit passes untouched, alone and in company.
    for (const std::uint16_t bit : {0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080,
                                    0x0100, 0x0200, 0x0400, 0x1000, 0x2000, 0x4000, 0x8000}) {
        CHECK(sanitizeButtonFlags(bit) == bit);
        CHECK(sanitizeButtonFlags(static_cast<std::uint16_t>(bit | 0x0800)) == bit);
    }
    CHECK(dish::moonlight::kSatelliteOnlyButtonBits == dish::input::layout::kXusbMicMute);
}
