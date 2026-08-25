// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Moonlight (GameStream) control-stream wire codec. This is the second
// connection path alongside the Satellite protocol-1 client: it speaks to any
// Moonlight-compatible host (Sunshine / Apollo / Wolf) rather than to a
// Satellite receiver.
//
// Struct layouts and constants are adapted from the MIT-licensed Wolf reference
// (games-on-whales/wolf, src/moonlight-protocol/moonlight/control.hpp and its
// protocol docs); see THIRD_PARTY.md. Nothing here is copied from the GPL
// moonlight-common-c / Sunshine / Apollo sources.
//
// This header is PURE: no OpenSSL, no Qt, no sockets. It encodes the plaintext
// control payloads and decodes host->client events. The AES-GCM sealing that
// wraps these payloads on the wire lives in MoonlightCrypto; the ENet transport
// in Network/MoonlightControlChannel.
//
// Endianness note: the outer control header (type, len) and the input-event
// payload bodies are LITTLE-endian. The two exceptions are the INPUT_DATA
// wrapper's `input size` field (BIG-endian u32) and mouse/relative deltas
// (BIG-endian i16); both are called out at their call sites.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace dish::moonlight {

// ── Outer control packet types (plaintext ptype, little-endian) ──────────────
inline constexpr std::uint16_t kCtrlEncrypted = 0x0001;
inline constexpr std::uint16_t kCtrlTermination = 0x0100;
inline constexpr std::uint16_t kCtrlPeriodicPing = 0x0200;
inline constexpr std::uint16_t kCtrlInputData = 0x0206;
inline constexpr std::uint16_t kCtrlRumbleData = 0x010b;
inline constexpr std::uint16_t kCtrlRumbleTriggers = 0x5500;
inline constexpr std::uint16_t kCtrlMotionEvent = 0x5501;
inline constexpr std::uint16_t kCtrlRgbLed = 0x5502;

// ── INPUT_DATA inner types (the u32 after the wrapper, little-endian) ─────────
inline constexpr std::uint32_t kInputMouseMoveRel = 0x00000007;
inline constexpr std::uint32_t kInputControllerMulti = 0x0000000C;
inline constexpr std::uint32_t kInputControllerArrival = 0x55000004;
inline constexpr std::uint32_t kInputControllerTouch = 0x55000005;
inline constexpr std::uint32_t kInputControllerMotion = 0x55000006;
inline constexpr std::uint32_t kInputControllerBattery = 0x55000007;

// ── CONTROLLER_ARRIVAL controller type (the "device to emulate" picker) ──────
inline constexpr std::uint8_t kPadTypeUnknown = 0x00;
inline constexpr std::uint8_t kPadTypeXbox = 0x01;
inline constexpr std::uint8_t kPadTypePlayStation = 0x02;
inline constexpr std::uint8_t kPadTypeNintendo = 0x03;

// ── CONTROLLER_ARRIVAL capability bits ───────────────────────────────────────
inline constexpr std::uint8_t kPadCapAnalogTriggers = 0x01;
inline constexpr std::uint8_t kPadCapRumble = 0x02;
inline constexpr std::uint8_t kPadCapTriggerRumble = 0x04;
inline constexpr std::uint8_t kPadCapTouchpad = 0x08;
inline constexpr std::uint8_t kPadCapAccel = 0x10;
inline constexpr std::uint8_t kPadCapGyro = 0x20;
inline constexpr std::uint8_t kPadCapBattery = 0x40;
inline constexpr std::uint8_t kPadCapRgbLed = 0x80;

// ── CONTROLLER_MULTI button flags (low 16 bits; paddles/touch are flags2) ─────
inline constexpr std::uint32_t kBtnDpadUp = 0x0001;
inline constexpr std::uint32_t kBtnDpadDown = 0x0002;
inline constexpr std::uint32_t kBtnDpadLeft = 0x0004;
inline constexpr std::uint32_t kBtnDpadRight = 0x0008;
inline constexpr std::uint32_t kBtnStart = 0x0010;
inline constexpr std::uint32_t kBtnBack = 0x0020;
inline constexpr std::uint32_t kBtnLeftStick = 0x0040;
inline constexpr std::uint32_t kBtnRightStick = 0x0080;
inline constexpr std::uint32_t kBtnLeftButton = 0x0100;
inline constexpr std::uint32_t kBtnRightButton = 0x0200;
inline constexpr std::uint32_t kBtnHome = 0x0400;
inline constexpr std::uint32_t kBtnA = 0x1000;
inline constexpr std::uint32_t kBtnB = 0x2000;
inline constexpr std::uint32_t kBtnX = 0x4000;
inline constexpr std::uint32_t kBtnY = 0x8000;
inline constexpr std::uint32_t kBtnTouchpad = 0x100000;
inline constexpr std::uint32_t kBtnMisc = 0x200000;

// MOTION_EVENT sensor selector (host asks the client to start streaming it).
inline constexpr std::uint8_t kMotionAccel = 0x01;
inline constexpr std::uint8_t kMotionGyro = 0x02;

// The exact byte length of a CONTROLLER_MULTI plaintext control payload, header
// included. Fixed so the hot path can preallocate: 4 (ptype+len) + 4 (input
// size BE) + 4 (input type LE) + 26 (struct) = 38.
inline constexpr std::size_t kControllerMultiBytes = 38;

// One controller's live state, the input to the hot-path encoder. Values map
// 1:1 onto the CONTROLLER_MULTI struct fields.
struct ControllerState {
    std::uint16_t controllerNumber = 0;
    // Bitfield of present controllers; dropping this controller's bit signals an
    // unplug. Bit (1 << controllerNumber) must be set while it is present.
    std::uint16_t activeGamepadMask = 0x0001;
    // effective = buttonFlags | (buttonFlags2 << 16). Callers fold paddles /
    // touchpad / misc into buttonFlags2.
    std::uint16_t buttonFlags = 0;
    std::uint16_t buttonFlags2 = 0;
    std::uint8_t leftTrigger = 0;
    std::uint8_t rightTrigger = 0;
    std::int16_t leftStickX = 0;
    std::int16_t leftStickY = 0;
    std::int16_t rightStickX = 0;
    std::int16_t rightStickY = 0;
};

// Hot path: encode `state` into `out` at fixed offsets, zero heap allocation.
// Returns kControllerMultiBytes. `out` must have room for kControllerMultiBytes.
// The result is the plaintext control payload ready to hand to sealControl.
std::size_t encodeControllerMulti(const ControllerState& state, std::uint8_t* out) noexcept;

// Convenience wrapper returning an owned buffer, for tests and cold callers.
std::vector<std::uint8_t> encodeControllerMulti(const ControllerState& state);

// CONTROLLER_ARRIVAL: announce a new virtual pad and its emulated type + caps.
std::vector<std::uint8_t> encodeControllerArrival(std::uint8_t controllerNumber,
                                                  std::uint8_t controllerType,
                                                  std::uint8_t capabilities,
                                                  std::uint32_t supportedButtons);

// CONTROLLER_MOTION: forward a gyro/accel sample (3 little-endian floats).
std::vector<std::uint8_t> encodeControllerMotion(std::uint8_t controllerNumber,
                                                 std::uint8_t motionType, float x, float y,
                                                 float z);

// CONTROLLER_BATTERY: forward the pad's charge state.
std::vector<std::uint8_t> encodeControllerBattery(std::uint8_t controllerNumber,
                                                  std::uint8_t batteryState,
                                                  std::uint8_t percentage);

// MOUSE_MOVE_REL: relative mouse delta. deltas are BIG-endian on the wire.
std::vector<std::uint8_t> encodeMouseMoveRel(std::int16_t dx, std::int16_t dy);

// PERIODIC_PING keepalive: header only, no payload.
std::vector<std::uint8_t> encodePeriodicPing();

// TERMINATION on graceful quit.
std::vector<std::uint8_t> encodeTermination();

// ── Host -> client events (decode of a decrypted control plaintext) ──────────

enum class ServerEventType {
    Rumble,
    RumbleTriggers,
    MotionEvent,
    RgbLed,
    Unknown, // a type we ignore gracefully
};

struct RumbleEvent {
    std::uint16_t controllerNumber = 0;
    std::uint16_t lowFreq = 0;
    std::uint16_t highFreq = 0;
};

struct RumbleTriggerEvent {
    std::uint16_t controllerNumber = 0;
    std::uint16_t left = 0;
    std::uint16_t right = 0;
};

struct MotionRequestEvent {
    std::uint16_t controllerNumber = 0;
    std::uint16_t reportRateHz = 0;
    std::uint8_t motionType = 0; // kMotionAccel | kMotionGyro
};

struct RgbLedEvent {
    std::uint16_t controllerNumber = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// The decoded server event. Only the variant matching `type` is populated.
struct ServerEvent {
    ServerEventType type = ServerEventType::Unknown;
    std::uint16_t rawType = 0;
    RumbleEvent rumble;
    RumbleTriggerEvent triggers;
    MotionRequestEvent motion;
    RgbLedEvent led;
};

// Decode a DECRYPTED control plaintext ([ptype u16 LE][plen u16 LE][body]).
// Returns nullopt only for a buffer too short to hold the header. An unknown or
// malformed-body type yields ServerEventType::Unknown (ignored gracefully),
// never nullopt, so the caller's receive loop treats "short header" (a torn
// packet) differently from "type I don't handle".
std::optional<ServerEvent> decodeServerEvent(const std::uint8_t* buf, std::size_t len);

} // namespace dish::moonlight
