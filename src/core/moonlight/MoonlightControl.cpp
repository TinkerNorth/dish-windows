// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightControl.h"

#include <cstring>

namespace dish::moonlight {

namespace {

// The four magic framing constants Moonlight stamps into every CONTROLLER_MULTI
// packet, verified byte-exact against Wolf's input-data fixture. header = the
// struct length marker (0x1A), mid = 0x14, and the tail pair 0x9C / 0x55.
constexpr std::uint16_t kMultiHeader = 0x001A;
constexpr std::uint16_t kMultiMid = 0x0014;
constexpr std::uint16_t kMultiTailA = 0x009C;
constexpr std::uint16_t kMultiTailB = 0x0055;

// Little-endian writers into a byte buffer at a fixed offset.
inline void putU16Le(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}

inline void putU32Le(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}

inline void putU32Be(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[3] = static_cast<std::uint8_t>(v & 0xFF);
}

inline std::uint16_t readU16Le(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

// Writes the standard INPUT_DATA wrapper into `out` and returns the offset past
// it. `bodyLen` is the number of payload bytes that FOLLOW the wrapper (the
// input-type-specific struct, NOT counting the 4-byte input-type field).
// Layout: [0x0206 LE][packet_len LE][input size BE][input type LE].
//   packet_len = 4 (input size) + 4 (input type) + bodyLen
//   input size = 4 (input type) + bodyLen   (BIG-endian, per the spec)
std::size_t writeInputHeader(std::uint8_t* out, std::uint32_t inputType,
                             std::size_t bodyLen) noexcept {
    const auto packetLen = static_cast<std::uint16_t>(8 + bodyLen);
    const auto inputSize = static_cast<std::uint32_t>(4 + bodyLen);
    putU16Le(out + 0, kCtrlInputData);
    putU16Le(out + 2, packetLen);
    putU32Be(out + 4, inputSize);
    putU32Le(out + 8, inputType);
    return 12;
}

// A bare control packet header: [type LE][len LE][body...]. `len` is the byte
// count of everything AFTER these first 4 bytes.
std::size_t writeControlHeader(std::uint8_t* out, std::uint16_t type,
                               std::size_t bodyLen) noexcept {
    putU16Le(out + 0, type);
    putU16Le(out + 2, static_cast<std::uint16_t>(bodyLen));
    return 4;
}

// float -> 4 little-endian bytes (the netfloat wire form).
void putFloatLe(std::uint8_t* p, float v) noexcept {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &v, sizeof(bits));
    putU32Le(p, bits);
}

} // namespace

std::size_t encodeControllerMulti(const ControllerState& s, std::uint8_t* out) noexcept {
    // 26-byte struct body follows the 4-byte input-type field.
    std::size_t off = writeInputHeader(out, kInputControllerMulti, 26);
    putU16Le(out + off, kMultiHeader);
    off += 2;
    putU16Le(out + off, s.controllerNumber);
    off += 2;
    putU16Le(out + off, s.activeGamepadMask);
    off += 2;
    putU16Le(out + off, kMultiMid);
    off += 2;
    putU16Le(out + off, s.buttonFlags);
    off += 2;
    out[off++] = s.leftTrigger;
    out[off++] = s.rightTrigger;
    putU16Le(out + off, static_cast<std::uint16_t>(s.leftStickX));
    off += 2;
    putU16Le(out + off, static_cast<std::uint16_t>(s.leftStickY));
    off += 2;
    putU16Le(out + off, static_cast<std::uint16_t>(s.rightStickX));
    off += 2;
    putU16Le(out + off, static_cast<std::uint16_t>(s.rightStickY));
    off += 2;
    putU16Le(out + off, kMultiTailA);
    off += 2;
    putU16Le(out + off, s.buttonFlags2);
    off += 2;
    putU16Le(out + off, kMultiTailB);
    off += 2;
    return off; // == kControllerMultiBytes
}

std::vector<std::uint8_t> encodeControllerMulti(const ControllerState& s) {
    std::vector<std::uint8_t> out(kControllerMultiBytes);
    encodeControllerMulti(s, out.data());
    return out;
}

std::vector<std::uint8_t> encodeControllerArrival(std::uint8_t controllerNumber,
                                                  std::uint8_t controllerType,
                                                  std::uint8_t capabilities,
                                                  std::uint32_t supportedButtons) {
    // body: ctrl#(1) + type(1) + cap(1) + reserved(1) + supportedButtons(4) = 8.
    std::vector<std::uint8_t> out(12 + kControllerArrivalBody);
    std::size_t off = writeInputHeader(out.data(), kInputControllerArrival, kControllerArrivalBody);
    out[off++] = controllerNumber;
    out[off++] = controllerType;
    out[off++] = capabilities;
    out[off++] = 0;
    putU32Le(out.data() + off, supportedButtons);
    return out;
}

std::vector<std::uint8_t> encodeControllerMotion(std::uint8_t controllerNumber,
                                                 std::uint8_t motionType, float x, float y,
                                                 float z) {
    // body: ctrl#(1) + motionType(1) + zero(2) + 3 netfloats(12) = 16.
    std::vector<std::uint8_t> out(12 + 16);
    std::size_t off = writeInputHeader(out.data(), kInputControllerMotion, 16);
    out[off++] = controllerNumber;
    out[off++] = motionType;
    out[off++] = 0;
    out[off++] = 0;
    putFloatLe(out.data() + off, x);
    off += 4;
    putFloatLe(out.data() + off, y);
    off += 4;
    putFloatLe(out.data() + off, z);
    return out;
}

std::vector<std::uint8_t> encodeControllerBattery(std::uint8_t controllerNumber,
                                                  std::uint8_t batteryState,
                                                  std::uint8_t percentage) {
    // body: ctrl#(1) + state(1) + percentage(1) + zero(1) = 4.
    std::vector<std::uint8_t> out(12 + 4);
    std::size_t off = writeInputHeader(out.data(), kInputControllerBattery, 4);
    out[off++] = controllerNumber;
    out[off++] = batteryState;
    out[off++] = percentage;
    out[off++] = 0;
    return out;
}

std::vector<std::uint8_t> encodeMouseMoveRel(std::int16_t dx, std::int16_t dy) {
    // body: dx(2 BE) + dy(2 BE) = 4.
    std::vector<std::uint8_t> out(12 + 4);
    std::size_t off = writeInputHeader(out.data(), kInputMouseMoveRel, 4);
    const auto udx = static_cast<std::uint16_t>(dx);
    const auto udy = static_cast<std::uint16_t>(dy);
    out[off++] = static_cast<std::uint8_t>((udx >> 8) & 0xFF);
    out[off++] = static_cast<std::uint8_t>(udx & 0xFF);
    out[off++] = static_cast<std::uint8_t>((udy >> 8) & 0xFF);
    out[off++] = static_cast<std::uint8_t>(udy & 0xFF);
    return out;
}

std::vector<std::uint8_t> encodePeriodicPing() {
    std::vector<std::uint8_t> out(4);
    writeControlHeader(out.data(), kCtrlPeriodicPing, 0);
    return out;
}

std::vector<std::uint8_t> encodeRtpPing(const std::string& pingPayload, std::uint32_t seq) {
    if (pingPayload.size() == 16) {
        std::vector<std::uint8_t> out(20);
        std::memcpy(out.data(), pingPayload.data(), 16);
        putU32Le(out.data() + 16, seq);
        return out;
    }
    return {'P', 'I', 'N', 'G'};
}

std::vector<std::uint8_t> encodeTermination() {
    // Wolf's ControlTerminatePacket carries a 4-byte reason (graceful).
    std::vector<std::uint8_t> out(4 + 4);
    std::size_t off = writeControlHeader(out.data(), kCtrlTermination, 4);
    // 0x80030023 big-endian (the "graceful" reason).
    putU32Be(out.data() + off, 0x80030023u);
    return out;
}

std::optional<ServerEvent> decodeServerEvent(const std::uint8_t* buf, std::size_t len) {
    if (buf == nullptr || len < 4) { return std::nullopt; }
    ServerEvent ev;
    ev.rawType = readU16Le(buf);
    const std::uint16_t bodyLen = readU16Le(buf + 2);
    const std::uint8_t* body = buf + 4;
    const std::size_t avail = len - 4;
    // A truncated body is treated as an unknown/ignored event, not a torn frame:
    // the caller already validated the GCM tag, so the length is authoritative
    // and a short body means a version we do not model.
    const std::size_t bl = bodyLen <= avail ? bodyLen : avail;

    switch (ev.rawType) {
    case kCtrlRumbleData:
        // unused(4) + ctrl(2) + low(2) + high(2)
        if (bl >= 10) {
            ev.type = ServerEventType::Rumble;
            ev.rumble.controllerNumber = readU16Le(body + 4);
            ev.rumble.lowFreq = readU16Le(body + 6);
            ev.rumble.highFreq = readU16Le(body + 8);
        }
        break;
    case kCtrlRumbleTriggers:
        // ctrl(2) + left(2) + right(2)
        if (bl >= 6) {
            ev.type = ServerEventType::RumbleTriggers;
            ev.triggers.controllerNumber = readU16Le(body + 0);
            ev.triggers.left = readU16Le(body + 2);
            ev.triggers.right = readU16Le(body + 4);
        }
        break;
    case kCtrlMotionEvent:
        // ctrl(2) + rate(2) + type(1)
        if (bl >= 5) {
            ev.type = ServerEventType::MotionEvent;
            ev.motion.controllerNumber = readU16Le(body + 0);
            ev.motion.reportRateHz = readU16Le(body + 2);
            ev.motion.motionType = body[4];
        }
        break;
    case kCtrlRgbLed:
        // ctrl(2) + r(1) + g(1) + b(1)
        if (bl >= 5) {
            ev.type = ServerEventType::RgbLed;
            ev.led.controllerNumber = readU16Le(body + 0);
            ev.led.r = body[2];
            ev.led.g = body[3];
            ev.led.b = body[4];
        }
        break;
    default:
        // Unknown or unhandled type: leave ServerEventType::Unknown.
        break;
    }
    return ev;
}

} // namespace dish::moonlight
