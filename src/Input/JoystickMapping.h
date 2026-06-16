// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "GamepadInputProcessor.h"

#include <cstdint>

namespace dish::input {

// Pure, SDL-free mapper for RAW SDL *joysticks* — generic pads that SDL
// classifies as a joystick but NOT a game controller (no entry in SDL's
// controller-mapping DB, e.g. vid 0x0079 "Generic USB Joystick"). The
// game-controller path (SDLGamepadBridge::rebuildState) handles recognised
// pads; this fills the gap for everything else with a best-guess default
// layout so the pad still surfaces as a slot and streams through the SAME
// GamepadInputProcessor::publish path.
//
// The function takes a raw joystick snapshot (axis int16 values, button bools,
// hat bitmasks — the exact shape SDL_JoystickGetAxis / GetButton / GetHat
// return) plus the device's reported counts, and produces the same normalised
// GamepadInputProcessor::DeviceState the controller path builds. It is total
// and deterministic: any index the layout references but the device does not
// provide reads as neutral (axis 0 / button released), so a pad with fewer
// axes or buttons than the default layout assumes can never crash or misread
// out-of-range memory — the caller passes the real counts and this clamps.
//
// ── DEFAULT LAYOUT (the common DirectInput / "Generic USB Joystick" order) ──
// Axes (int16, SDL range [-32768, 32767]):
//   axis 0 = left stick  X        axis 1 = left stick  Y
//   With >= 6 axes:  axis 3 = right stick X, axis 4 = right stick Y,
//                    axis 2 = left trigger,  axis 5 = right trigger.
//   With 4 or 5 axes: axis 2 = right stick X, axis 3 = right stick Y
//                    (no dedicated trigger axes → triggers come from buttons).
// Buttons (the DirectInput face-first order most generic pads expose):
//   0 = A (south)   1 = B (east)   2 = X (west)   3 = Y (north)
//   4 = left shoulder    5 = right shoulder
//   6 = back/select      7 = start
//   8 = guide            9 = (unused)
//   10 = left stick click   11 = right stick click
//   When there are no dedicated trigger axes, buttons 6/7 are reused for the
//   triggers and the shoulders shift — see kHasTriggerAxes handling below.
// Hat 0 → dpad (SDL_HAT_* bitmask; 8-way).
//
// This layout is a deliberate best guess: real generic pads vary wildly, so a
// future per-device remap UI will override it. See TODO(remap) below.

// Raw joystick snapshot. Pointers + counts mirror what SDL exposes for an open
// SDL_Joystick (SDL_JoystickNumAxes / NumButtons / NumHats and the per-index
// getters). Kept SDL-free: the bridge fills this from SDL, the mapper never
// touches SDL. A null pointer with a non-zero count is treated as "all
// neutral" so the mapper stays total even on a malformed snapshot.
struct JoystickSnapshot {
    const std::int16_t* axes = nullptr; // length = axisCount
    int axisCount = 0;
    const bool* buttons = nullptr; // length = buttonCount
    int buttonCount = 0;
    const std::uint8_t* hats = nullptr; // length = hatCount (SDL_HAT_* bitmask)
    int hatCount = 0;
};

// SDL_HAT_* bit values, redeclared here so this header stays SDL-free. These
// are stable wire-level constants in SDL2 (SDL_hat.h) — not an implementation
// detail that can drift — so mirroring them is safe.
namespace hat {
constexpr std::uint8_t kCentered = 0x00;
constexpr std::uint8_t kUp = 0x01;
constexpr std::uint8_t kRight = 0x02;
constexpr std::uint8_t kDown = 0x04;
constexpr std::uint8_t kLeft = 0x08;
} // namespace hat

// Map a raw joystick snapshot to the normalised gamepad report. Pure, total,
// deterministic. The Y axes are inverted to match the controller path (SDL Y
// is +down; the XUSB report the processor consumes expects +up).
GamepadInputProcessor::DeviceState mapJoystick(const JoystickSnapshot& snap);

// ── Internal helpers, exposed for unit tests (pure) ─────────────────────────

// Safe axis read: returns 0 for any out-of-range index or null array so the
// mapper never reads past the device's real axis count.
std::int16_t axisAt(const JoystickSnapshot& snap, int index);

// Safe button read: returns false for any out-of-range index or null array.
bool buttonAt(const JoystickSnapshot& snap, int index);

// Convert a signed int16 axis [-32768, 32767] to the 0..255 trigger scale the
// report uses (negative → 0). Matches the controller path's triggerValue
// shape (which scales 0..32767 → 0..255); here the full signed span is mapped
// so a centre-resting trigger axis (~0) reads as a half-pressed trigger only
// if the pad genuinely rests its trigger at centre — most generic pads rest a
// trigger axis at the negative extreme, which maps to 0.
std::uint8_t triggerFromAxis(std::int16_t v);

// True iff the snapshot has dedicated trigger axes under the default layout
// (>= 6 axes). Drives whether triggers come from axes or from buttons.
bool hasTriggerAxes(const JoystickSnapshot& snap);

} // namespace dish::input
