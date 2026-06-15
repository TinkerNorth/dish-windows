// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbReportParsersTest — per-model HID input-report decode vectors for the
// Windows USB-direct path (core/input/UsbReportParsers.h). Each case feeds a
// KNOWN report byte vector (synthetic, built from the documented layout the
// decoder mirrors from dish-android usb_parsers.cpp) and asserts the decoded
// XUSB buttons / sticks / triggers / IMU / touchpad. No hardware, no IO.
//
// The button/stick/trigger offsets are the load-bearing part and match android's
// usb_parsers.cpp byte-for-byte; the DS4/DualSense IMU + touchpad offsets are the
// public hid-playstation layout (NEEDS-REAL-PAD sign/scale check, flagged in the
// header) — the tests pin the OFFSETS + the present/absent gating, not a
// hardware-true magnitude.

#include "core/input/UsbReportParsers.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace dish::input::usbparse;
namespace layout = dish::input::layout;

namespace {

ParsedReport decode(HidParser p, const std::vector<std::uint8_t>& buf) {
    ParsedReport out{};
    StickAutoRangeState sticks;
    const bool ok = decodeReport(p, buf.data(), buf.size(), out, sticks);
    out.wButtons = ok ? out.wButtons : 0xDEAD; // sentinel so a false return is visible.
    return out;
}

} // namespace

// ── parser selection ──────────────────────────────────────────────────────────

TEST_CASE("parserForDevice picks the right family by VID PID", "[usb-parsers]") {
    CHECK(parserForDevice(0x054C, 0x0CE6) == HidParser::DualSense);    // DualSense
    CHECK(parserForDevice(0x054C, 0x0DF2) == HidParser::DualSense);    // DualSense Edge
    CHECK(parserForDevice(0x054C, 0x05C4) == HidParser::DualShock4);   // DS4 v1
    CHECK(parserForDevice(0x054C, 0x09CC) == HidParser::DualShock4);   // DS4 v2
    CHECK(parserForDevice(0x057E, 0x2009) == HidParser::SwitchProUsb); // Switch Pro
    CHECK(parserForDevice(0x2DC8, 0x9015) == HidParser::GenericHid);   // 8BitDo
    CHECK(parserForDevice(0x1234, 0x5678) == HidParser::GenericHid);   // unknown
}

TEST_CASE("parser capability predicates agree with the families", "[usb-parsers]") {
    CHECK(parserHasImu(HidParser::DualShock4));
    CHECK(parserHasImu(HidParser::DualSense));
    CHECK(parserHasImu(HidParser::SwitchProUsb));
    CHECK_FALSE(parserHasImu(HidParser::GenericHid));
    CHECK_FALSE(parserHasImu(HidParser::None));
    CHECK(parserHasTouchpad(HidParser::DualShock4));
    CHECK(parserHasTouchpad(HidParser::DualSense));
    CHECK_FALSE(parserHasTouchpad(HidParser::SwitchProUsb));
    CHECK_FALSE(parserHasTouchpad(HidParser::GenericHid));
}

// ── DualShock 4 (report 0x01) ──────────────────────────────────────────────────

TEST_CASE("DualShock4 decodes Cross to XUSB A and centers neutral sticks", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x01;
    r[1] = 128;  // LX center
    r[2] = 128;  // LY center
    r[3] = 128;  // RX center
    r[4] = 128;  // RY center
    r[5] = 0x20; // Cross -> XUSB_A; hat low nibble 0 would be N, but 0x20 low nibble = 0.
    // Low nibble of 0x20 is 0 => hat octant 0 (Up). Use 0x28 to also exercise hat
    // below; here keep a pure face-button check by masking hat to neutral (0x08).
    r[5] = static_cast<std::uint8_t>(0x20 | 0x08); // Cross + hat nibble 8 (neutral).
    const auto out = decode(HidParser::DualShock4, r);
    CHECK((out.wButtons & layout::kXusbA) != 0);
    CHECK((out.wButtons & layout::kXusbDpadUp) == 0);
    CHECK(out.lx == 0);
    CHECK(out.ly == 0);
    CHECK(out.rx == 0);
    CHECK(out.ry == 0);
}

TEST_CASE("DualShock4 maps every face and shoulder button by position", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[5] = 0x08;                                    // hat neutral, no face bits.
    r[5] |= 0x10;                                   // Square -> X
    r[5] |= 0x80;                                   // Triangle -> Y
    r[6] = 0x01 | 0x02 | 0x10 | 0x20 | 0x40 | 0x80; // L1,R1,Share,Options,L3,R3
    const auto out = decode(HidParser::DualShock4, r);
    CHECK((out.wButtons & layout::kXusbX) != 0);
    CHECK((out.wButtons & layout::kXusbY) != 0);
    CHECK((out.wButtons & layout::kXusbLeftShoulder) != 0);
    CHECK((out.wButtons & layout::kXusbRightShoulder) != 0);
    CHECK((out.wButtons & layout::kXusbBack) != 0);  // Share
    CHECK((out.wButtons & layout::kXusbStart) != 0); // Options
    CHECK((out.wButtons & layout::kXusbLeftThumb) != 0);
    CHECK((out.wButtons & layout::kXusbRightThumb) != 0);
}

TEST_CASE("DualShock4 inverts the Y axis and scales triggers", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x01;
    r[1] = 255; // LX full right -> large positive
    r[2] = 0;   // LY raw min; inverted -> large positive
    r[3] = 128;
    r[4] = 255; // RY raw max; inverted -> large negative
    r[5] = 0x08;
    r[8] = 200; // L2
    r[9] = 50;  // R2
    const auto out = decode(HidParser::DualShock4, r);
    CHECK(out.lx > 30000);  // 255 -> +32639
    CHECK(out.ly > 30000);  // inverted 0 -> +32896 clamped to +32767
    CHECK(out.ry < -30000); // inverted 255 -> -32639
    CHECK(out.lt == 200);
    CHECK(out.rt == 50);
}

TEST_CASE("DualShock4 decodes the hat to dpad bits", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[5] = 0x02; // hat octant 2 = East -> dpad right
    const auto out = decode(HidParser::DualShock4, r);
    CHECK((out.wButtons & layout::kXusbDpadRight) != 0);
    CHECK((out.wButtons & layout::kXusbDpadUp) == 0);
}

TEST_CASE("DualShock4 a short report has no IMU or touchpad", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[5] = 0x08;
    const auto out = decode(HidParser::DualShock4, r);
    CHECK(out.wButtons != 0xDEAD); // decoded ok
    CHECK_FALSE(out.motionValid);
    CHECK_FALSE(out.touchpadValid);
}

TEST_CASE("DualShock4 a full report carries IMU and touchpad", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(64, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[5] = 0x08;
    // gyro X at 13/14 (LE 0x0100 = 256), accel X at 19/20 (LE 0x0010 = 16).
    r[13] = 0x00;
    r[14] = 0x01;
    r[19] = 0x10;
    r[20] = 0x00;
    // Touch finger 0 at byte 35: id 3, touching (bit7 clear), x=0x010, y=0x020.
    r[35] = 0x03;
    r[36] = 0x10; // x low
    r[37] = 0x20; // x high nibble 0, y low nibble 2
    r[38] = 0x00; // y high
    // Finger 1 not touching (bit7 set).
    r[39] = 0x80;
    const auto out = decode(HidParser::DualShock4, r);
    REQUIRE(out.motionValid);
    CHECK(out.gyroX != 0);
    CHECK(out.accelX != 0);
    REQUIRE(out.touchpadValid);
    CHECK(out.finger0Active);
    CHECK(out.finger0Id == 3);
    CHECK_FALSE(out.finger1Active);
}

TEST_CASE("DualShock4 rejects a wrong report id", "[usb-parsers][ds4]") {
    std::vector<std::uint8_t> r(10, 0);
    r[0] = 0x11; // not 0x01
    const auto out = decode(HidParser::DualShock4, r);
    CHECK(out.wButtons == 0xDEAD); // false return
}

// ── DualSense (report 0x01) ─────────────────────────────────────────────────────

TEST_CASE("DualSense decodes Cross to XUSB A with shifted trigger and button bytes",
          "[usb-parsers][dualsense]") {
    std::vector<std::uint8_t> r(11, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[5] = 150;                                    // L2 analog at byte 5 (DualSense, not byte 8)
    r[6] = 40;                                     // R2 analog at byte 6
    r[8] = static_cast<std::uint8_t>(0x20 | 0x08); // Cross -> A, hat neutral
    r[9] = 0x20;                                   // Options -> Start
    const auto out = decode(HidParser::DualSense, r);
    CHECK((out.wButtons & layout::kXusbA) != 0);
    CHECK((out.wButtons & layout::kXusbStart) != 0);
    CHECK(out.lt == 150);
    CHECK(out.rt == 40);
    CHECK(out.lx == 0);
}

TEST_CASE("DualSense maps shoulders thumbs and Share by position", "[usb-parsers][dualsense]") {
    std::vector<std::uint8_t> r(11, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[8] = 0x08;                             // hat neutral
    r[8] |= 0x40;                            // Circle -> B
    r[9] = 0x01 | 0x02 | 0x10 | 0x40 | 0x80; // L1,R1,Create,L3,R3
    const auto out = decode(HidParser::DualSense, r);
    CHECK((out.wButtons & layout::kXusbB) != 0);
    CHECK((out.wButtons & layout::kXusbLeftShoulder) != 0);
    CHECK((out.wButtons & layout::kXusbRightShoulder) != 0);
    CHECK((out.wButtons & layout::kXusbBack) != 0); // Create
    CHECK((out.wButtons & layout::kXusbLeftThumb) != 0);
    CHECK((out.wButtons & layout::kXusbRightThumb) != 0);
}

TEST_CASE("DualSense a full report carries IMU and touchpad", "[usb-parsers][dualsense]") {
    std::vector<std::uint8_t> r(48, 0);
    r[0] = 0x01;
    r[1] = r[2] = r[3] = r[4] = 128;
    r[8] = 0x08;
    r[16] = 0x00; // gyro X LE
    r[17] = 0x02; // 0x0200
    r[22] = 0x40; // accel X LE
    r[23] = 0x00; // 0x0040
    r[33] = 0x05; // touch finger 0 id 5, touching
    r[34] = 0x20;
    r[35] = 0x10;
    r[36] = 0x00;
    r[37] = 0x80; // finger 1 not touching
    const auto out = decode(HidParser::DualSense, r);
    REQUIRE(out.motionValid);
    CHECK(out.gyroX != 0);
    CHECK(out.accelX != 0);
    REQUIRE(out.touchpadValid);
    CHECK(out.finger0Active);
    CHECK(out.finger0Id == 5);
    CHECK_FALSE(out.finger1Active);
}

// ── Switch Pro (report 0x30) ────────────────────────────────────────────────────

TEST_CASE("SwitchPro maps face buttons by physical position and digital triggers",
          "[usb-parsers][switch]") {
    std::vector<std::uint8_t> r(25, 0);
    r[0] = 0x30;
    // right byte: bit0 X(->XUSB_X), bit2 A(->XUSB_A), bit3 B(->XUSB_B), bit6 R, bit7 ZR
    r[3] = 0x01 | 0x04 | 0x80;
    // shared: bit1 Plus(->Start), bit4 Home(->Guide)
    r[4] = 0x02 | 0x10;
    // left: bit1 Up(->dpad up), bit6 L, bit7 ZL
    r[5] = 0x02 | 0x80;
    // sticks centered (2048 packed): LX=2048 -> buf6=0x00, buf7 low nibble=0x08
    r[6] = 0x00;
    r[7] = 0x80; // ly high nibble | lx high nibble -> ly low=8<<? keep near center
    r[8] = 0x08;
    r[9] = 0x00;
    r[10] = 0x80;
    r[11] = 0x08;
    const auto out = decode(HidParser::SwitchProUsb, r);
    CHECK((out.wButtons & layout::kXusbX) != 0);
    CHECK((out.wButtons & layout::kXusbA) != 0);
    CHECK((out.wButtons & layout::kXusbStart) != 0);
    CHECK((out.wButtons & layout::kXusbGuide) != 0);
    CHECK((out.wButtons & layout::kXusbDpadUp) != 0);
    CHECK(out.rt == 255); // ZR digital -> full
    CHECK(out.lt == 255); // ZL digital -> full
}

TEST_CASE("SwitchPro a full report decodes the first IMU frame", "[usb-parsers][switch]") {
    std::vector<std::uint8_t> r(25, 0);
    r[0] = 0x30;
    // accel X at 13/14 (LE 0x0100), gyro X at 19/20 (LE 0x0080).
    r[13] = 0x00;
    r[14] = 0x01;
    r[19] = 0x80;
    r[20] = 0x00;
    const auto out = decode(HidParser::SwitchProUsb, r);
    REQUIRE(out.motionValid);
    CHECK(out.accelX != 0);
    CHECK(out.gyroX != 0);
}

TEST_CASE("SwitchPro rejects a short report", "[usb-parsers][switch]") {
    std::vector<std::uint8_t> r(11, 0);
    r[0] = 0x30;
    const auto out = decode(HidParser::SwitchProUsb, r);
    CHECK(out.wButtons == 0xDEAD);
}

// ── Generic HID / 8BitDo ────────────────────────────────────────────────────────

TEST_CASE("GenericHid decodes axes hat face and shoulder buttons", "[usb-parsers][generic]") {
    std::vector<std::uint8_t> r(7, 0);
    r[0] = 128;                                           // LX center
    r[1] = 128;                                           // LY center
    r[2] = 128;                                           // RX center
    r[3] = 128;                                           // RY center
    r[4] = static_cast<std::uint8_t>(0x08 | 0x10 | 0x20); // hat neutral + A + B
    r[5] = 0x01 | 0x02 | 0x40 | 0x80;                     // LB, RB, LT, RT
    const auto out = decode(HidParser::GenericHid, r);
    CHECK((out.wButtons & layout::kXusbA) != 0);
    CHECK((out.wButtons & layout::kXusbB) != 0);
    CHECK((out.wButtons & layout::kXusbLeftShoulder) != 0);
    CHECK((out.wButtons & layout::kXusbRightShoulder) != 0);
    CHECK(out.lt == 255);
    CHECK(out.rt == 255);
    CHECK(out.lx == 0);
}

TEST_CASE("GenericHid never reports IMU or touchpad", "[usb-parsers][generic]") {
    std::vector<std::uint8_t> r(7, 0);
    r[0] = r[1] = r[2] = r[3] = 128;
    r[4] = 0x08;
    const auto out = decode(HidParser::GenericHid, r);
    CHECK_FALSE(out.motionValid);
    CHECK_FALSE(out.touchpadValid);
}

TEST_CASE("GenericHid rejects a too-short report", "[usb-parsers][generic]") {
    std::vector<std::uint8_t> r(6, 0);
    const auto out = decode(HidParser::GenericHid, r);
    CHECK(out.wButtons == 0xDEAD);
}

// ── scalar helpers ──────────────────────────────────────────────────────────────

TEST_CASE("scaleU8Centered centers and inverts", "[usb-parsers][scale]") {
    CHECK(scaleU8Centered(128, false) == 0);
    CHECK(scaleU8Centered(128, true) == 0);
    CHECK(scaleU8Centered(255, false) > 32000);
    CHECK(scaleU8Centered(0, false) < -32000);
    // Inverted sense flips the sign.
    CHECK(scaleU8Centered(0, true) > 32000);
    CHECK(scaleU8Centered(255, true) < -32000);
}

TEST_CASE("scaleSwitchStickAuto deadzones the center and ranges the throw",
          "[usb-parsers][scale]") {
    AxisAutoRange axis;
    CHECK(scaleSwitchStickAuto(2048, axis) == 0);                 // dead center
    CHECK(scaleSwitchStickAuto(2048 + 100, axis) == 0);           // inside deadzone (<=320)
    const std::int16_t pushed = scaleSwitchStickAuto(4095, axis); // full positive
    CHECK(pushed > 0);
    const std::int16_t pulled = scaleSwitchStickAuto(0, axis); // full negative
    CHECK(pulled < 0);
}
