// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbReportParsers — the PURE, Qt-free, allocation-free per-model HID input-report
// decoders for the Windows USB-direct (raw-HID) path. Each decoder is a pure
// function (raw report bytes -> ParsedReport struct) so it is host-testable with
// synthetic byte vectors and never touches the OS. WinHidGateway::readLoop calls
// these on its plain-C++ read thread; the manager publishes the result straight
// into GamepadInputProcessor with no per-report heap allocation.
//
// ── Byte offsets MIRROR dish-android/app/src/main/cpp/usb_parsers.cpp 1:1 ──────
// The DualShock 4 (report 0x01), DualSense (report 0x01), Switch Pro (report
// 0x30), and generic-HID/8BitDo decoders below reproduce android's documented
// byte offsets exactly. The mappings:
//   * Face buttons map by PHYSICAL POSITION, not label — Cross/B(bottom)->XUSB_A,
//     Circle/A(right)->XUSB_B, Square/Y(left)->XUSB_X, Triangle/X(top)->XUSB_Y —
//     so PC games / ViGEm see the XInput muscle-memory layout (matching android).
//   * Sticks: PlayStation pads report uint8 centred at 128; Y axes are
//     down-positive on the wire so they are inverted to XUSB's up-positive
//     convention (scaleU8Centered(invert=true)). Switch Pro packs 12-bit values
//     and auto-ranges each side of centre (asymmetric throw).
//   * IMU: Switch Pro 0x30 carries three 12-byte IMU frames starting at byte 13;
//     we decode the first (accel int16 LE +0/+2/+4, gyro +6/+8/+10) and scale to
//     the wire. DS4/DualSense ALSO carry an IMU + touchpad in their USB reports —
//     android does NOT decode those on its USB path (parserHasImu==SWITCH only),
//     but the Windows TOUCHPAD/IMU inversions (§4/§6 of the hot-path comparison)
//     mean we DO decode the DS4/DualSense gyro+accel+touchpad here so the
//     USB-direct path reaches parity with the SDL path's motion+touchpad streams.
//
// ── HARDWARE-VALIDATION CAVEAT ────────────────────────────────────────────────
// The face-button / stick / trigger offsets are taken from android's
// usb_parsers.cpp, which is itself hardware-validated. The DS4/DualSense IMU +
// touchpad offsets here are derived from the PUBLIC hid-playstation report layout
// (the same source android's docs/rumble.md cites) but are NOT exercised by
// android's USB path, so their exact sign/scale needs a final check against real
// pads. Each such field is flagged inline. The button/stick/trigger decode is the
// load-bearing part and matches android byte-for-byte.

#pragma once

#include "core/input/GamepadButtonLayouts.h"

#include <cstddef>
#include <cstdint>

namespace dish::input::usbparse {

// Which per-model decoder a claimed HID pad uses. A narrower set than android's
// usbparsers::Parser: Windows raw-HID never sees Xbox pads (XInput hides them) or
// the Stadia/GIP families on this path, so only the HID-class families that can
// actually be claimed are represented. NONE means "no decodable family".
enum class HidParser : std::uint8_t {
    None = 0,
    DualShock4,
    DualSense,
    SwitchProUsb,
    GenericHid,
};

// Per-device, expand-only stick auto-range for the Switch Pro (raw 12-bit ADC,
// asymmetric throw, no factory calibration read). Mirrors android
// usbparsers::AxisAutoRange + ParserState (the stick half only — the Windows
// path has no Xbox-GIP guide-merge state to carry). One instance lives per
// claimed device in the gateway's read loop; it is mutated in place each report,
// so the decode stays allocation-free.
struct AxisAutoRange {
    std::int32_t posReach = 1000;
    std::int32_t negReach = 1000;
};

struct StickAutoRangeState {
    AxisAutoRange lx;
    AxisAutoRange ly;
    AxisAutoRange rx;
    AxisAutoRange ry;
};

// A decoded controller report, normalised to the XUSB axis/trigger scale (the
// same wButtons word GamepadInputProcessor publishes). IMU + touchpad fields are
// populated only when the model + report carry them (motionValid / touch*Valid).
struct ParsedReport {
    std::uint16_t wButtons = 0; // XUSB button bits (GamepadButtonLayouts kXusb*).
    std::uint8_t lt = 0;
    std::uint8_t rt = 0;
    std::int16_t lx = 0;
    std::int16_t ly = 0;
    std::int16_t rx = 0;
    std::int16_t ry = 0;

    // IMU (wire int16 scale, gyro deg/s/2000*32767, accel g/4*32767). Valid only
    // when motionValid is set (the model has an IMU and the report carried it).
    bool motionValid = false;
    std::int16_t gyroX = 0;
    std::int16_t gyroY = 0;
    std::int16_t gyroZ = 0;
    std::int16_t accelX = 0;
    std::int16_t accelY = 0;
    std::int16_t accelZ = 0;

    // Touchpad (DS4 / DualSense). Coordinates are normalised to the same signed
    // int16 span SdlMotionConvert::touchpadCoordToInt16 produces (0 -> -32768,
    // max -> +32767). touchpadValid is set when the report layout carried the
    // touchpad block (so a generic pad never emits a spurious touchpad sample).
    bool touchpadValid = false;
    bool finger0Active = false;
    std::uint8_t finger0Id = 0;
    std::int16_t finger0X = 0;
    std::int16_t finger0Y = 0;
    bool finger1Active = false;
    std::uint8_t finger1Id = 0;
    std::int16_t finger1X = 0;
    std::int16_t finger1Y = 0;
    bool touchpadButton = false;
};

// ── Pure scalar helpers (mirror usb_parsers.cpp's static helpers) ─────────────

// Map a uint8 stick value centred at 128 to a full-range XUSB int16 axis. When
// `invert` is set the sense is flipped (PlayStation Y axes are down-positive on
// the wire; XUSB wants up-positive). Matches scaleU8Centered exactly: s*257 with
// saturation to the int16 range.
inline std::int16_t scaleU8Centered(std::uint8_t v, bool invert) {
    std::int32_t s =
        invert ? (128 - static_cast<std::int32_t>(v)) : (static_cast<std::int32_t>(v) - 128);
    std::int32_t scaled = s * 257;
    if (scaled > 32767) { scaled = 32767; }
    if (scaled < -32768) { scaled = -32768; }
    return static_cast<std::int16_t>(scaled);
}

// Inner deadzone in the raw 12-bit Switch domain (mirrors android's
// kSwitchStickRawDeadzone). The Pro's centre wanders per unit and we read no
// factory calibration, so counts within the deadzone read as centre.
inline constexpr std::int32_t kSwitchStickRawDeadzone = 320;

// Map a 12-bit Switch stick value (centred near 2048) to a full-range XUSB axis,
// auto-ranging each side independently. 1:1 with scaleSwitchStickAuto.
inline std::int16_t scaleSwitchStickAuto(std::uint16_t raw12, AxisAutoRange& axis) {
    std::int32_t centered = static_cast<std::int32_t>(raw12) - 2048;
    std::int32_t mag = centered >= 0 ? centered : -centered;
    if (mag <= kSwitchStickRawDeadzone) { return 0; }
    std::int32_t adj = mag - kSwitchStickRawDeadzone;
    if (centered >= 0) {
        if (adj > axis.posReach) { axis.posReach = adj; }
        std::int32_t scaled = (adj * 32767) / axis.posReach;
        if (scaled > 32767) { scaled = 32767; }
        return static_cast<std::int16_t>(scaled);
    }
    if (adj > axis.negReach) { axis.negReach = adj; }
    std::int32_t scaled = (adj * 32768) / axis.negReach;
    if (scaled > 32768) { scaled = 32768; }
    return static_cast<std::int16_t>(-scaled);
}

// Read a little-endian int16 at offset `off` (mirrors rdLe16).
inline std::int16_t rdLe16(const std::uint8_t* b, int off) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(b[off]) |
                                     (static_cast<std::uint16_t>(b[off + 1]) << 8));
}

// Switch Pro IMU default scaling, no factory calibration (mirrors
// switchGyroToWire / switchAccelToWire: combined integer factors 32767/28568 and
// 32767/16384).
inline std::int16_t switchGyroToWire(std::int16_t raw) {
    std::int64_t wire = static_cast<std::int64_t>(raw) * 32767 / 28568;
    if (wire > 32767) { wire = 32767; }
    if (wire < -32768) { wire = -32768; }
    return static_cast<std::int16_t>(wire);
}

inline std::int16_t switchAccelToWire(std::int16_t raw) {
    std::int64_t wire = static_cast<std::int64_t>(raw) * 32767 / 16384;
    if (wire > 32767) { wire = 32767; }
    if (wire < -32768) { wire = -32768; }
    return static_cast<std::int16_t>(wire);
}

// Fold a 4-bit HID hat value (0..7 clockwise from N, >=8 neutral) into XUSB dpad
// bits. Mirrors setDpadFromHat exactly.
inline std::uint16_t setDpadFromHat(std::uint16_t buttons, std::uint8_t hat) {
    buttons =
        static_cast<std::uint16_t>(buttons & ~(layout::kXusbDpadUp | layout::kXusbDpadDown |
                                               layout::kXusbDpadLeft | layout::kXusbDpadRight));
    switch (hat & 0x0F) {
    case 0:
        buttons |= layout::kXusbDpadUp;
        break;
    case 1:
        buttons |= static_cast<std::uint16_t>(layout::kXusbDpadUp | layout::kXusbDpadRight);
        break;
    case 2:
        buttons |= layout::kXusbDpadRight;
        break;
    case 3:
        buttons |= static_cast<std::uint16_t>(layout::kXusbDpadDown | layout::kXusbDpadRight);
        break;
    case 4:
        buttons |= layout::kXusbDpadDown;
        break;
    case 5:
        buttons |= static_cast<std::uint16_t>(layout::kXusbDpadDown | layout::kXusbDpadLeft);
        break;
    case 6:
        buttons |= layout::kXusbDpadLeft;
        break;
    case 7:
        buttons |= static_cast<std::uint16_t>(layout::kXusbDpadUp | layout::kXusbDpadLeft);
        break;
    default:
        break;
    }
    return buttons;
}

// Convert a 0..1919/1079-ish absolute touchpad coordinate to the signed int16
// span the SDL path uses (touchpadCoordToInt16: 0 -> -32768, full -> +32767).
// `value` / `maxValue` is the normalised position. Pure integer math (no float)
// so it stays allocation- and FPU-free on the read thread.
inline std::int16_t touchAbsToInt16(std::int32_t value, std::int32_t maxValue) {
    if (maxValue <= 0) { return 0; }
    if (value < 0) { value = 0; }
    if (value > maxValue) { value = maxValue; }
    // Map [0, maxValue] -> [-32768, 32767] (range 65535).
    const std::int64_t span = 65535;
    std::int64_t scaled = (static_cast<std::int64_t>(value) * span) / maxValue - 32768;
    if (scaled > 32767) { scaled = 32767; }
    if (scaled < -32768) { scaled = -32768; }
    return static_cast<std::int16_t>(scaled);
}

// ── Per-model decoders ────────────────────────────────────────────────────────

// DualShock 4 USB report 0x01. Byte offsets mirror usb_parsers.cpp decodeDualShock4:
//   buf[0]=0x01 report id; buf[1..4]=LX/LY/RX/RY (uint8, 128 centre, Y inverted);
//   buf[5] low nibble=hat, bits: 0x10 Square(->X) 0x20 Cross(->A) 0x40 Circle(->B)
//   0x80 Triangle(->Y); buf[6]: 0x01 L1 0x02 R1 0x10 Share(Back) 0x20 Options(Start)
//   0x40 L3 0x80 R3; buf[8]=L2 analog buf[9]=R2 analog.
// The DS4 full USB report ALSO carries an IMU (gyro int16 LE at buf[13..], accel at
// buf[19..]) and a 2-finger touchpad block (buf[35..]). android does NOT decode
// these on its USB path; we add them for the Windows motion/touchpad parity. The
// IMU/touchpad offsets are from the public hid-playstation layout and need a final
// real-pad sign/scale check (flagged below).
inline bool decodeDualShock4(const std::uint8_t* buf, std::size_t len, ParsedReport& s,
                             StickAutoRangeState& /*unused*/) {
    if (len < 10) { return false; }
    if (buf[0] != 0x01) { return false; }

    s.lx = scaleU8Centered(buf[1], false);
    s.ly = scaleU8Centered(buf[2], true);
    s.rx = scaleU8Centered(buf[3], false);
    s.ry = scaleU8Centered(buf[4], true);

    std::uint16_t b = 0;
    if (buf[5] & 0x10) { b |= layout::kXusbX; }
    if (buf[5] & 0x20) { b |= layout::kXusbA; }
    if (buf[5] & 0x40) { b |= layout::kXusbB; }
    if (buf[5] & 0x80) { b |= layout::kXusbY; }
    if (buf[6] & 0x01) { b |= layout::kXusbLeftShoulder; }
    if (buf[6] & 0x02) { b |= layout::kXusbRightShoulder; }
    if (buf[6] & 0x10) { b |= layout::kXusbBack; }
    if (buf[6] & 0x20) { b |= layout::kXusbStart; }
    if (buf[6] & 0x40) { b |= layout::kXusbLeftThumb; }
    if (buf[6] & 0x80) { b |= layout::kXusbRightThumb; }
    // The PS/Guide button is bit 0x01 of buf[7] on the DS4 USB report (not decoded
    // by android's USB path; included for parity with the SDL guide mapping).
    if (len > 7 && (buf[7] & 0x01)) { b |= layout::kXusbGuide; }
    b = setDpadFromHat(b, static_cast<std::uint8_t>(buf[5] & 0x0F));
    s.wButtons = b;

    s.lt = buf[8];
    s.rt = buf[9];

    // IMU (NEEDS-REAL-PAD validation of sign/scale): DS4 full report carries gyro
    // (int16 LE) at buf[13/15/17] and accel at buf[19/21/23]. Reuse the Switch
    // wire-scale factors as a placeholder conversion — the DS4 LSB differs, so the
    // magnitude will need a real-pad calibration pass; the offsets are the public
    // hid-playstation layout.
    if (len >= 25) {
        s.gyroX = switchGyroToWire(rdLe16(buf, 13));
        s.gyroY = switchGyroToWire(rdLe16(buf, 15));
        s.gyroZ = switchGyroToWire(rdLe16(buf, 17));
        s.accelX = switchAccelToWire(rdLe16(buf, 19));
        s.accelY = switchAccelToWire(rdLe16(buf, 21));
        s.accelZ = switchAccelToWire(rdLe16(buf, 23));
        s.motionValid = true;
    }

    // Touchpad (NEEDS-REAL-PAD validation): DS4 touch block begins at buf[35].
    // Layout per finger (4 bytes): [0] = id|active(bit7 clear == touching), [1] =
    // x low 8, [2] = (y low nibble << 4)|(x high nibble), [3] = y high 8. Finger 0
    // at buf[35..38], finger 1 at buf[39..42]. DS4 touchpad is 1920x942.
    if (len >= 43) {
        const std::uint8_t* t0 = buf + 35;
        const std::uint8_t* t1 = buf + 39;
        const bool f0Touch = (t0[0] & 0x80) == 0;
        const bool f1Touch = (t1[0] & 0x80) == 0;
        s.finger0Active = f0Touch;
        s.finger0Id = static_cast<std::uint8_t>(t0[0] & 0x7F);
        s.finger0X = touchAbsToInt16(static_cast<std::int32_t>(t0[1]) |
                                         ((static_cast<std::int32_t>(t0[2]) & 0x0F) << 8),
                                     1919);
        s.finger0Y = touchAbsToInt16(
            (static_cast<std::int32_t>(t0[2]) >> 4) | (static_cast<std::int32_t>(t0[3]) << 4), 941);
        s.finger1Active = f1Touch;
        s.finger1Id = static_cast<std::uint8_t>(t1[0] & 0x7F);
        s.finger1X = touchAbsToInt16(static_cast<std::int32_t>(t1[1]) |
                                         ((static_cast<std::int32_t>(t1[2]) & 0x0F) << 8),
                                     1919);
        s.finger1Y = touchAbsToInt16(
            (static_cast<std::int32_t>(t1[2]) >> 4) | (static_cast<std::int32_t>(t1[3]) << 4), 941);
        // The clickable pad is a button bit in buf[7] (0x02) on the DS4 report.
        s.touchpadButton = len > 7 && (buf[7] & 0x02) != 0;
        s.touchpadValid = true;
    }
    return true;
}

// DualSense USB report 0x01. Byte offsets mirror usb_parsers.cpp decodeDualSense:
//   buf[0]=0x01; buf[1..4]=LX/LY/RX/RY (uint8, 128 centre, Y inverted); buf[5]=L2
//   buf[6]=R2 analog; buf[8] face/hat (low nibble hat, 0x10 Square/X 0x20 Cross/A
//   0x40 Circle/B 0x80 Triangle/Y); buf[9]: 0x01 L1 0x02 R1 0x10 Create(Back) 0x20
//   Options(Start) 0x40 L3 0x80 R3.
// The DualSense full report carries an IMU (gyro at buf[16..], accel at buf[22..])
// and a 2-finger touchpad (buf[33..]). android does NOT decode these on its USB
// path; added for parity (offsets from the public hid-playstation layout, flagged
// NEEDS-REAL-PAD).
inline bool decodeDualSense(const std::uint8_t* buf, std::size_t len, ParsedReport& s,
                            StickAutoRangeState& /*unused*/) {
    if (len < 11) { return false; }
    if (buf[0] != 0x01) { return false; }

    s.lx = scaleU8Centered(buf[1], false);
    s.ly = scaleU8Centered(buf[2], true);
    s.rx = scaleU8Centered(buf[3], false);
    s.ry = scaleU8Centered(buf[4], true);

    s.lt = buf[5];
    s.rt = buf[6];

    std::uint16_t b = 0;
    if (buf[8] & 0x10) { b |= layout::kXusbX; }
    if (buf[8] & 0x20) { b |= layout::kXusbA; }
    if (buf[8] & 0x40) { b |= layout::kXusbB; }
    if (buf[8] & 0x80) { b |= layout::kXusbY; }
    if (buf[9] & 0x01) { b |= layout::kXusbLeftShoulder; }
    if (buf[9] & 0x02) { b |= layout::kXusbRightShoulder; }
    if (buf[9] & 0x10) { b |= layout::kXusbBack; }
    if (buf[9] & 0x20) { b |= layout::kXusbStart; }
    if (buf[9] & 0x40) { b |= layout::kXusbLeftThumb; }
    if (buf[9] & 0x80) { b |= layout::kXusbRightThumb; }
    // PS button: buf[10] bit 0x01 on the DualSense report.
    if (len > 10 && (buf[10] & 0x01)) { b |= layout::kXusbGuide; }
    b = setDpadFromHat(b, static_cast<std::uint8_t>(buf[8] & 0x0F));
    s.wButtons = b;

    // IMU (NEEDS-REAL-PAD validation of sign/scale): DualSense gyro int16 LE at
    // buf[16/18/20], accel at buf[22/24/26]. Placeholder wire-scale (see DS4 note).
    if (len >= 28) {
        s.gyroX = switchGyroToWire(rdLe16(buf, 16));
        s.gyroY = switchGyroToWire(rdLe16(buf, 18));
        s.gyroZ = switchGyroToWire(rdLe16(buf, 20));
        s.accelX = switchAccelToWire(rdLe16(buf, 22));
        s.accelY = switchAccelToWire(rdLe16(buf, 24));
        s.accelZ = switchAccelToWire(rdLe16(buf, 26));
        s.motionValid = true;
    }

    // Touchpad (NEEDS-REAL-PAD validation): DualSense touch block begins at buf[33]
    // (same 4-byte-per-finger packing as DS4). DualSense touchpad is 1920x1080.
    if (len >= 41) {
        const std::uint8_t* t0 = buf + 33;
        const std::uint8_t* t1 = buf + 37;
        s.finger0Active = (t0[0] & 0x80) == 0;
        s.finger0Id = static_cast<std::uint8_t>(t0[0] & 0x7F);
        s.finger0X = touchAbsToInt16(static_cast<std::int32_t>(t0[1]) |
                                         ((static_cast<std::int32_t>(t0[2]) & 0x0F) << 8),
                                     1919);
        s.finger0Y = touchAbsToInt16((static_cast<std::int32_t>(t0[2]) >> 4) |
                                         (static_cast<std::int32_t>(t0[3]) << 4),
                                     1079);
        s.finger1Active = (t1[0] & 0x80) == 0;
        s.finger1Id = static_cast<std::uint8_t>(t1[0] & 0x7F);
        s.finger1X = touchAbsToInt16(static_cast<std::int32_t>(t1[1]) |
                                         ((static_cast<std::int32_t>(t1[2]) & 0x0F) << 8),
                                     1919);
        s.finger1Y = touchAbsToInt16((static_cast<std::int32_t>(t1[2]) >> 4) |
                                         (static_cast<std::int32_t>(t1[3]) << 4),
                                     1079);
        // DualSense pad-click is bit 0x02 of buf[10].
        s.touchpadButton = len > 10 && (buf[10] & 0x02) != 0;
        s.touchpadValid = true;
    }
    return true;
}

// Switch Pro standard full input report 0x30 over USB. Byte offsets + the
// position-based XUSB mapping mirror usb_parsers.cpp decodeSwitchProUsb 1:1:
//   buf[0]=0x30; buf[3]=right buttons, buf[4]=shared, buf[5]=left; sticks packed
//   12-bit at buf[6..11]; ZL/ZR digital; IMU frames from buf[13].
inline bool decodeSwitchProUsb(const std::uint8_t* buf, std::size_t len, ParsedReport& s,
                               StickAutoRangeState& sticks) {
    if (len < 12) { return false; }
    if (buf[0] != 0x30) { return false; }

    const std::uint8_t br = buf[3];
    const std::uint8_t bs = buf[4];
    const std::uint8_t bl = buf[5];

    std::uint16_t b = 0;
    if (br & 0x01) { b |= layout::kXusbX; }
    if (br & 0x02) { b |= layout::kXusbY; }
    if (br & 0x04) { b |= layout::kXusbA; }
    if (br & 0x08) { b |= layout::kXusbB; }
    if (br & 0x40) { b |= layout::kXusbRightShoulder; }
    if (bs & 0x01) { b |= layout::kXusbBack; }
    if (bs & 0x02) { b |= layout::kXusbStart; }
    if (bs & 0x04) { b |= layout::kXusbRightThumb; }
    if (bs & 0x08) { b |= layout::kXusbLeftThumb; }
    if (bs & 0x10) { b |= layout::kXusbGuide; } // Home is bs 0x10 on the Pro.
    if (bl & 0x01) { b |= layout::kXusbDpadDown; }
    if (bl & 0x02) { b |= layout::kXusbDpadUp; }
    if (bl & 0x04) { b |= layout::kXusbDpadRight; }
    if (bl & 0x08) { b |= layout::kXusbDpadLeft; }
    if (bl & 0x40) { b |= layout::kXusbLeftShoulder; }
    s.wButtons = b;

    // ZL/ZR are digital on the Pro (fully pressed or released).
    s.lt = (bl & 0x80) ? 255 : 0;
    s.rt = (br & 0x80) ? 255 : 0;

    const std::uint16_t lx =
        static_cast<std::uint16_t>(buf[6]) | ((static_cast<std::uint16_t>(buf[7]) & 0x0F) << 8);
    const std::uint16_t ly =
        (static_cast<std::uint16_t>(buf[7]) >> 4) | (static_cast<std::uint16_t>(buf[8]) << 4);
    const std::uint16_t rx =
        static_cast<std::uint16_t>(buf[9]) | ((static_cast<std::uint16_t>(buf[10]) & 0x0F) << 8);
    const std::uint16_t ry =
        (static_cast<std::uint16_t>(buf[10]) >> 4) | (static_cast<std::uint16_t>(buf[11]) << 4);

    s.lx = scaleSwitchStickAuto(lx, sticks.lx);
    s.ly = scaleSwitchStickAuto(ly, sticks.ly);
    s.rx = scaleSwitchStickAuto(rx, sticks.rx);
    s.ry = scaleSwitchStickAuto(ry, sticks.ry);

    // IMU: three 12-byte frames start at byte 13 (accel int16 LE at +0/+2/+4, gyro
    // at +6/+8/+10). Use the first frame. Straight axis mapping; signs may need an
    // on-device flip to match the wire convention (mirrors android's note).
    if (len >= 25) {
        s.accelX = switchAccelToWire(rdLe16(buf, 13));
        s.accelY = switchAccelToWire(rdLe16(buf, 15));
        s.accelZ = switchAccelToWire(rdLe16(buf, 17));
        s.gyroX = switchGyroToWire(rdLe16(buf, 19));
        s.gyroY = switchGyroToWire(rdLe16(buf, 21));
        s.gyroZ = switchGyroToWire(rdLe16(buf, 23));
        s.motionValid = true;
    }
    return true;
}

// Generic HID / 8BitDo best-effort decoder. Byte offsets mirror
// decodeGenericHidGamepad: 4 axes at buf[0..3] (uint8 centred, Y inverted), hat in
// buf[4] low nibble, face buttons in buf[4] high nibble, shoulders/thumbs/triggers
// in buf[5]. Conservative shape check (>= 7 bytes) so non-gamepad reports bail.
inline bool decodeGenericHid(const std::uint8_t* buf, std::size_t len, ParsedReport& s,
                             StickAutoRangeState& /*unused*/) {
    if (len < 7) { return false; }
    s.lx = scaleU8Centered(buf[0], false);
    s.ly = scaleU8Centered(buf[1], true);
    s.rx = scaleU8Centered(buf[2], false);
    s.ry = scaleU8Centered(buf[3], true);
    std::uint16_t b = 0;
    b = setDpadFromHat(b, static_cast<std::uint8_t>(buf[4] & 0x0F));
    const std::uint8_t btnLo = buf[4];
    const std::uint8_t btnHi = len > 5 ? buf[5] : 0;
    if (btnLo & 0x10) { b |= layout::kXusbA; }
    if (btnLo & 0x20) { b |= layout::kXusbB; }
    if (btnLo & 0x40) { b |= layout::kXusbX; }
    if (btnLo & 0x80) { b |= layout::kXusbY; }
    if (btnHi & 0x01) { b |= layout::kXusbLeftShoulder; }
    if (btnHi & 0x02) { b |= layout::kXusbRightShoulder; }
    if (btnHi & 0x04) { b |= layout::kXusbBack; }
    if (btnHi & 0x08) { b |= layout::kXusbStart; }
    if (btnHi & 0x10) { b |= layout::kXusbLeftThumb; }
    if (btnHi & 0x20) { b |= layout::kXusbRightThumb; }
    s.wButtons = b;
    s.lt = (btnHi & 0x40) ? 255 : 0;
    s.rt = (btnHi & 0x80) ? 255 : 0;
    return true;
}

// Dispatch on the chosen family. Returns false (leaving `out` untouched in its
// caller's intent) when the family is None or the report is too short / wrong id.
inline bool decodeReport(HidParser parser, const std::uint8_t* buf, std::size_t len,
                         ParsedReport& out, StickAutoRangeState& sticks) {
    switch (parser) {
    case HidParser::DualShock4:
        return decodeDualShock4(buf, len, out, sticks);
    case HidParser::DualSense:
        return decodeDualSense(buf, len, out, sticks);
    case HidParser::SwitchProUsb:
        return decodeSwitchProUsb(buf, len, out, sticks);
    case HidParser::GenericHid:
        return decodeGenericHid(buf, len, out, sticks);
    case HidParser::None:
        return false;
    }
    return false;
}

// Choose the decoder family from the device VID:PID. Sony (0x054C) splits by PID:
// the DualSense (0x0CE6 / Edge 0x0DF2) uses the DualSense layout, every other Sony
// pad (DualShock 4 family) the DS4 layout. Nintendo (0x057E) is the Switch Pro.
// Everything else falls back to the generic-HID best-effort decoder. Mirrors the
// android lookupKnown -> parser selection narrowed to the HID-claimable families.
inline HidParser parserForDevice(int vendorId, int productId) {
    constexpr int kVidSony = 0x054C;
    constexpr int kVidNintendo = 0x057E;
    constexpr int kPidDualSense = 0x0CE6;
    constexpr int kPidDualSenseEdge = 0x0DF2;
    if (vendorId == kVidSony) {
        if (productId == kPidDualSense || productId == kPidDualSenseEdge) {
            return HidParser::DualSense;
        }
        return HidParser::DualShock4;
    }
    if (vendorId == kVidNintendo) { return HidParser::SwitchProUsb; }
    return HidParser::GenericHid;
}

// Whether a family carries an in-band IMU we decode (DS4 / DualSense / Switch Pro).
inline bool parserHasImu(HidParser parser) {
    return parser == HidParser::DualShock4 || parser == HidParser::DualSense ||
           parser == HidParser::SwitchProUsb;
}

// Whether a family carries a touchpad block we decode (DS4 / DualSense).
inline bool parserHasTouchpad(HidParser parser) {
    return parser == HidParser::DualShock4 || parser == HidParser::DualSense;
}

} // namespace dish::input::usbparse
