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
#include <string>
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
// ── CONTROLLER_TOUCH event types (control.hpp TOUCH_EVENT_TYPE) ──────────────
// Shared by the touchscreen, pen and controller-touchpad surfaces. Only the
// three a two-finger pad can produce are modelled; HOVER and the pen-only
// values have no source here.
inline constexpr std::uint8_t kTouchEventDown = 0x01;
inline constexpr std::uint8_t kTouchEventUp = 0x02;
inline constexpr std::uint8_t kTouchEventMove = 0x03;

inline constexpr std::uint8_t kMotionAccel = 0x01;
inline constexpr std::uint8_t kMotionGyro = 0x02;

// CONTROLLER_BATTERY state byte.
inline constexpr std::uint8_t kBatteryStateUnknown = 0x00;
inline constexpr std::uint8_t kBatteryStateNotPresent = 0x01;
inline constexpr std::uint8_t kBatteryStateDischarging = 0x02;
inline constexpr std::uint8_t kBatteryStateCharging = 0x03;
inline constexpr std::uint8_t kBatteryStateNotCharging = 0x04;
inline constexpr std::uint8_t kBatteryStateFull = 0x05;
inline constexpr std::uint8_t kBatteryPercentageUnknown = 0xFF;

// Map the Satellite wire battery status (0 unknown, 1 discharging, 2 charging,
// 3 full, 4 wired) onto the Moonlight battery state, so the SDL battery stream
// forwards to either host kind from one publisher.
inline std::uint8_t batteryStateFromSatelliteStatus(std::uint8_t satelliteStatus) {
    switch (satelliteStatus) {
    case 1:
        return kBatteryStateDischarging;
    case 2:
        return kBatteryStateCharging;
    case 3:
        return kBatteryStateFull;
    case 4:
        // Wired with no charge telemetry reads as charging, the closest state.
        return kBatteryStateCharging;
    default:
        return kBatteryStateUnknown;
    }
}

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

// The processor's XUSB button word is bit-for-bit the layout Moonlight's low
// 16 button flags use, EXCEPT for one bit: 0x0800 has no XINPUT assignment and
// protocol 2 spends it on the DualSense mic-mute STATE
// (input::layout::kXusbMicMute), a Satellite-only signal a GameStream host
// would misread. Every buttonFlags fold goes through this so the bit can never
// leak to a Moonlight host however it got into the word.
inline constexpr std::uint16_t kSatelliteOnlyButtonBits = 0x0800;

inline constexpr std::uint16_t sanitizeButtonFlags(std::uint16_t xusbButtons) {
    return static_cast<std::uint16_t>(xusbButtons & ~kSatelliteOnlyButtonBits);
}

// Hot path: encode `state` into `out` at fixed offsets, zero heap allocation.
// Returns kControllerMultiBytes. `out` must have room for kControllerMultiBytes.
// The result is the plaintext control payload ready to hand to sealControl.
std::size_t encodeControllerMulti(const ControllerState& state, std::uint8_t* out) noexcept;

// Convenience wrapper returning an owned buffer, for tests and cold callers.
std::vector<std::uint8_t> encodeControllerMulti(const ControllerState& state);

// The CONTROLLER_ARRIVAL body length. EIGHT BYTES, NOT SEVEN. Its fields are a
// u8 number, a u8 type, a u8 capability bitfield and a u32 button mask, which
// add up to seven; but the host reads them out of a NATURALLY ALIGNED struct, so
// the u32 starts at offset 4 and offset 3 is reserved padding. Sending seven
// shifts everything after the type by one byte: a live Sunshine host read a
// capabilities word of 0x03 as 0xFF03 and a 0xFFFF button mask as 0x000000FF,
// and logged `capabilities [FF03] supportedButtonFlags [000000FF]`.
inline constexpr std::size_t kControllerArrivalBody = 8;

// CONTROLLER_ARRIVAL: announce a new virtual pad and its emulated type + caps.
std::vector<std::uint8_t> encodeControllerArrival(std::uint8_t controllerNumber,
                                                  std::uint8_t controllerType,
                                                  std::uint8_t capabilities,
                                                  std::uint32_t supportedButtons);

// CONTROLLER_TOUCH: one pointer event on the emulated pad's touch surface.
// `x` / `y` are normalised 0..1 across the pad (the host multiplies by its
// emulated touchpad's resolution) and `pressure` is 1.0 for a solid contact,
// 0.0 on release. Unlike the satellite's full-state frame this is an EVENT
// stream, so the caller diffs first (MoonlightTouchDiffer.h).
std::vector<std::uint8_t> encodeControllerTouch(std::uint8_t controllerNumber,
                                                std::uint8_t eventType, std::uint32_t pointerId,
                                                float x, float y, float pressure);

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

// The RTP client ping datagram for the video/audio UDP ports: the host learns
// the client's address from it and gates media startup on its arrival. With a
// 16-char X-SS-Ping-Payload from SETUP it is the 20-byte session-id form
// [payload 16][seq u32 LE]; otherwise the 4-byte legacy "PING".
std::vector<std::uint8_t> encodeRtpPing(const std::string& pingPayload, std::uint32_t seq);

// ── Host -> client events (decode of a decrypted control plaintext) ──────────

enum class ServerEventType {
    Rumble,
    RumbleTriggers,
    MotionEvent,
    RgbLed,
    // The host ending the session on purpose. The same 0x0100 the client sends
    // to end one, and the only thing that tells a deliberate stop apart from a
    // link that simply went away.
    Termination,
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
