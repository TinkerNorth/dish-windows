// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "GamepadInputProcessor.h"

#include <array>
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

// ── Per-device REMAP ────────────────────────────────────────────────────────
// A JoystickRemap is the user-correctable routing table the "Configure controls"
// page edits (android parity). It describes which RAW source (axis / button /
// hat index) drives each logical output, so a user can fix a pad whose generic
// DirectInput order is scrambled. The DEFAULT value reproduces the historical
// hard-coded layout 1:1, so a device with no stored remap behaves exactly as
// before (and the default-layout contract in test_joystick_mapping.cpp stays
// green unchanged).

// Where a logical trigger reads its value from. Generic pads expose a trigger
// either as a dedicated analogue axis or as a digital button; the remap tags
// which, so triggerFromAxis / full-scale-on-press is chosen per device.
enum class TriggerSourceKind { Axis, Button };

struct TriggerSource {
    TriggerSourceKind kind = TriggerSourceKind::Axis;
    int index = -1; // -1 = unassigned → reads neutral (trigger stays 0)

    bool operator==(const TriggerSource& o) const { return kind == o.kind && index == o.index; }
    bool operator!=(const TriggerSource& o) const { return !(*this == o); }
};

// Count of logical buttons routed by the remap. Indexed by the logical-button
// enumerators below (kButtonCount is NOT a button — it sizes the array).
enum class RemapButton : int {
    DpadUp = 0,
    DpadDown,
    DpadLeft,
    DpadRight,
    Start,
    Back,
    LeftThumb,
    RightThumb,
    LeftShoulder,
    RightShoulder,
    A,
    B,
    X,
    Y,
    kButtonCount // sentinel: array size, not a real button
};
constexpr int kRemapButtonCount = static_cast<int>(RemapButton::kButtonCount);

// The routing table. Every field defaults to the historical layout, so a
// default-constructed JoystickRemap == today's hard-coded behaviour. Indices
// are RAW source indices into the snapshot; -1 means "unassigned" → the logical
// output reads neutral. The mapper is total: an index the device does not
// provide reads neutral via axisAt / buttonAt, never out-of-range memory.
struct JoystickRemap {
    // Sticks. Right-stick defaults are 3/4 — the 6-axis (dedicated-trigger-axes)
    // layout. The historical code adaptively used 2/3 when a pad reports < 6
    // axes; that adaptive fallback is preserved (see useAdaptiveRightStick) so
    // the default reproduces BOTH the 6-axis and the 4/5-axis behaviour. A user
    // who edits these fields opts out of the adaptive fallback (their explicit
    // choice is honoured verbatim).
    int leftStickX = 0;
    int leftStickY = 1;
    int rightStickX = 3;
    int rightStickY = 4;
    bool invertLeftY = true;  // SDL +down → report +up (historical hard invert)
    bool invertRightY = true; // same

    // Triggers. Defaults match the historical >=6-axis layout (axes 2/5). The
    // adaptive fallback for < 6 axes (buttons 8/9 at full scale) is preserved
    // under the default; an explicit edit disables it (see useAdaptiveTriggers).
    TriggerSource leftTrigger{TriggerSourceKind::Axis, 2};
    TriggerSource rightTrigger{TriggerSourceKind::Axis, 5};

    // Logical button → raw source-button index (-1 = unassigned → released).
    // Default order is the historical DirectInput face-first map: A/B/X/Y on
    // 0-3, shoulders 4/5, back/start 6/7, stick clicks 10/11. Dpad entries
    // default to -1 because the dpad comes from the HAT, not a button (a remap
    // MAY route a dpad direction to a button instead).
    std::array<int, kRemapButtonCount> buttons{
        /*DpadUp*/ -1,        /*DpadDown*/ -1, /*DpadLeft*/ -1, /*DpadRight*/ -1,
        /*Start*/ 7,          /*Back*/ 6,      /*LeftThumb*/ 10, /*RightThumb*/ 11,
        /*LeftShoulder*/ 4,   /*RightShoulder*/ 5,
        /*A*/ 0,              /*B*/ 1,         /*X*/ 2,         /*Y*/ 3};

    int hatIndex = 0; // raw hat index that drives the dpad (-1 = no hat dpad)

    // The two flags below are NOT user-editable fields; they are TRUE only for
    // the default routing and FALSE the moment a user customises the relevant
    // group. They gate the historical adaptive (< 6 axes) behaviour so the
    // single default value reproduces both the 6-axis and the 4/5-axis paths,
    // while an explicit user remap is applied verbatim.
    bool useAdaptiveRightStick = true; // default right stick falls back to 2/3 on < 6 axes
    bool useAdaptiveTriggers = true;   // default triggers fall back to buttons 8/9 on < 6 axes

    bool operator==(const JoystickRemap& o) const {
        return leftStickX == o.leftStickX && leftStickY == o.leftStickY &&
               rightStickX == o.rightStickX && rightStickY == o.rightStickY &&
               invertLeftY == o.invertLeftY && invertRightY == o.invertRightY &&
               leftTrigger == o.leftTrigger && rightTrigger == o.rightTrigger &&
               buttons == o.buttons && hatIndex == o.hatIndex &&
               useAdaptiveRightStick == o.useAdaptiveRightStick &&
               useAdaptiveTriggers == o.useAdaptiveTriggers;
    }
    bool operator!=(const JoystickRemap& o) const { return !(*this == o); }
};

// ── Capture → assignment (pure, testable) ───────────────────────────────────
// The "Configure controls" page runs a CAPTURE: the bridge streams the raw
// input the user just pressed (kind 0=axis, 1=button, 2=hat + the source index)
// and the page routes it to a logical OUTPUT the user is currently assigning.
// RemapTarget enumerates every assignable output; withAssignment is the pure
// reducer that folds one capture into a JoystickRemap. Keeping it here (SDL-free)
// makes the whole capture→remap step unit-testable without a live device.

// Kind tags for a raw capture event — mirror the bridge's rawJoystickInput
// `kind` argument so the UI never hard-codes the integers.
enum class CaptureKind : int { Axis = 0, Button = 1, Hat = 2 };

// Every logical output the remap page can (re)assign from a capture. The four
// invert toggles are NOT here — they are booleans, not a captured source, so
// they have their own setter (withInvert) rather than flowing through a capture.
enum class RemapTarget : int {
    A = 0,
    B,
    X,
    Y,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    LeftShoulder,
    RightShoulder,
    Back,
    Start,
    LeftThumb,
    RightThumb,
    LeftStickX,
    LeftStickY,
    RightStickX,
    RightStickY,
    LeftTrigger,
    RightTrigger,
};

// Which invert flag a withInvert call toggles. Kept separate from RemapTarget
// because an invert is a boolean edit, not a captured source.
enum class InvertTarget : int { LeftY = 0, RightY };

// Apply ONE capture result (kind + raw source index) to `base` and return the
// new remap. Pure + total: routing a button-kind capture to a STICK/axis target
// (or an axis-kind to a digital BUTTON target) is honoured verbatim — the page,
// not this function, decides what makes sense; here we only record the user's
// choice. A capture to a trigger target tags the TriggerSource kind from the
// capture kind (Axis-kind → analogue axis source, Button-kind → digital button
// source) so a pad whose trigger is a button decodes at full-scale-on-press.
// Assigning any of the stick/trigger/right-stick targets clears the matching
// adaptive fallback flag, so an explicit user choice is applied verbatim instead
// of being overridden by the historical < 6-axis fallback (parity with the
// JoystickRemap field comments). A Hat-kind capture to a DPAD target routes the
// dpad to that hat index (and clears any button override for that direction);
// a Button-kind capture to a DPAD target routes that direction to the button.
JoystickRemap withAssignment(JoystickRemap base, RemapTarget target, int kind, int index);

// Toggle one invert flag. Pure; returns the edited remap.
JoystickRemap withInvert(JoystickRemap base, InvertTarget which, bool on);

// Deliberate-press gate for an AXIS capture: true iff |value| exceeds the
// stick-flat-scaled threshold, so resting-axis jitter never registers as a
// press. Button / hat captures do NOT use this (they pass on any press / any
// non-centered direction) — see captureButtonPasses / captureHatPasses. `value`
// is the raw int16 axis reading. The threshold is a multiple of the default
// stick flat (kDefaultStickFlat) so it sits well above idle noise.
bool captureAxisPasses(int value);

// A button capture always registers a deliberate press (SDL only raises
// SDL_JOYBUTTONDOWN on a real press). Provided as a named predicate so the
// bridge and the tests share one definition.
bool captureButtonPasses();

// A hat capture registers iff the direction is non-centered (a release to
// center is not an assignment). `hatValue` is the SDL_HAT_* bitmask.
bool captureHatPasses(int hatValue);

// Map a raw joystick snapshot to the normalised gamepad report under `remap`.
// Pure, total, deterministic. Unassigned (-1) targets read neutral.
GamepadInputProcessor::DeviceState mapJoystick(const JoystickSnapshot& snap,
                                               const JoystickRemap& remap);

// Map under the DEFAULT remap (the historical hard-coded layout). Kept as an
// overload so existing callers and the default-layout contract test are
// unchanged. The Y axes are inverted to match the controller path (SDL Y is
// +down; the XUSB report the processor consumes expects +up).
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
