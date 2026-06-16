// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "JoystickMapping.h"

namespace dish::input {

std::int16_t axisAt(const JoystickSnapshot& snap, int index) {
    // Out-of-range / null reads neutral so a pad with fewer axes than the
    // default layout assumes never reads past its real axis array.
    if (snap.axes == nullptr || index < 0 || index >= snap.axisCount) { return 0; }
    return snap.axes[index];
}

bool buttonAt(const JoystickSnapshot& snap, int index) {
    if (snap.buttons == nullptr || index < 0 || index >= snap.buttonCount) { return false; }
    return snap.buttons[index];
}

std::uint8_t triggerFromAxis(std::int16_t v) {
    // Negative (and centre-or-below) → released. The positive half of the
    // int16 span maps linearly to 0..255 — same shape the controller path
    // uses for the 0..32767 trigger axes.
    if (v <= 0) { return 0; }
    return static_cast<std::uint8_t>((static_cast<int>(v) * 255) / 32767);
}

bool hasTriggerAxes(const JoystickSnapshot& snap) {
    // Six or more axes is the DirectInput signature for "two sticks + two
    // dedicated trigger axes". Four/five axes is "two sticks, triggers are
    // buttons".
    return snap.axisCount >= 6;
}

namespace {

// Drive one logical button from its remapped source index. -1 (unassigned)
// reads neutral so the bit stays clear.
void applyButton(std::uint16_t& btn, std::uint16_t bit, const JoystickSnapshot& snap, int source) {
    if (source >= 0 && buttonAt(snap, source)) { btn |= bit; }
}

// Resolve a trigger source to its 0..255 value. An Axis source scales through
// triggerFromAxis (same shape the controller path uses); a Button source is
// full-scale on press. An unassigned (-1) source reads neutral.
std::uint8_t triggerValue(const JoystickSnapshot& snap, const TriggerSource& src) {
    if (src.index < 0) { return 0; }
    if (src.kind == TriggerSourceKind::Axis) { return triggerFromAxis(axisAt(snap, src.index)); }
    return buttonAt(snap, src.index) ? 255 : 0;
}

} // namespace

GamepadInputProcessor::DeviceState mapJoystick(const JoystickSnapshot& snap,
                                               const JoystickRemap& remap) {
    using B = GamepadInputProcessor::Buttons;
    GamepadInputProcessor::DeviceState st{};

    // Under the DEFAULT remap a pad with < 6 axes used right stick 2/3 and
    // sourced triggers from buttons 8/9. The adaptive flags preserve that for
    // the default while an explicit user remap is honoured verbatim. Compute
    // the fallback once so right-stick and trigger paths share it.
    const bool fewAxes = !hasTriggerAxes(snap);

    // ── Sticks ──────────────────────────────────────────────────────────────
    st.lx = remap.leftStickX >= 0 ? axisAt(snap, remap.leftStickX) : 0;
    {
        const std::int16_t ly = remap.leftStickY >= 0 ? axisAt(snap, remap.leftStickY) : 0;
        st.ly = remap.invertLeftY ? static_cast<std::int16_t>(-ly) : ly;
    }
    int rightX = remap.rightStickX;
    int rightY = remap.rightStickY;
    if (remap.useAdaptiveRightStick && fewAxes) {
        // Historical 4/5-axis fallback: right stick on 2/3.
        rightX = 2;
        rightY = 3;
    }
    st.rx = rightX >= 0 ? axisAt(snap, rightX) : 0;
    {
        const std::int16_t ry = rightY >= 0 ? axisAt(snap, rightY) : 0;
        st.ry = remap.invertRightY ? static_cast<std::int16_t>(-ry) : ry;
    }

    // ── Buttons ─────────────────────────────────────────────────────────────
    std::uint16_t btn = 0;
    const auto src = [&](RemapButton b) { return remap.buttons[static_cast<int>(b)]; };
    applyButton(btn, B::kDpadUp, snap, src(RemapButton::DpadUp));
    applyButton(btn, B::kDpadDown, snap, src(RemapButton::DpadDown));
    applyButton(btn, B::kDpadLeft, snap, src(RemapButton::DpadLeft));
    applyButton(btn, B::kDpadRight, snap, src(RemapButton::DpadRight));
    applyButton(btn, B::kStart, snap, src(RemapButton::Start));
    applyButton(btn, B::kBack, snap, src(RemapButton::Back));
    applyButton(btn, B::kLeftThumb, snap, src(RemapButton::LeftThumb));
    applyButton(btn, B::kRightThumb, snap, src(RemapButton::RightThumb));
    applyButton(btn, B::kLeftShoulder, snap, src(RemapButton::LeftShoulder));
    applyButton(btn, B::kRightShoulder, snap, src(RemapButton::RightShoulder));
    applyButton(btn, B::kA, snap, src(RemapButton::A));
    applyButton(btn, B::kB, snap, src(RemapButton::B));
    applyButton(btn, B::kX, snap, src(RemapButton::X));
    applyButton(btn, B::kY, snap, src(RemapButton::Y));
    // The guide/home key (historically button 8) maps to no XUSB bit; it is
    // simply not routed to any logical button, so it stays dropped here.

    // ── Triggers ────────────────────────────────────────────────────────────
    if (remap.useAdaptiveTriggers && fewAxes) {
        // Historical 4/5-axis fallback: triggers from buttons 8/9 at full scale.
        // (See the long-form rationale that lived here before: a generic 4-axis
        // pad cannot disambiguate L2/R2 vs back/start statically; this is the
        // ambiguity the per-device remap resolves.)
        st.lt = buttonAt(snap, 8) ? 255 : 0;
        st.rt = buttonAt(snap, 9) ? 255 : 0;
    } else {
        st.lt = triggerValue(snap, remap.leftTrigger);
        st.rt = triggerValue(snap, remap.rightTrigger);
    }

    // ── Hat → dpad ──────────────────────────────────────────────────────────
    // SDL_HAT_* is a bitmask so diagonals (e.g. up-right) set two dpad bits. A
    // negative hatIndex (or a pad with no hat) leaves the dpad clear.
    const std::uint8_t h = (remap.hatIndex >= 0 && snap.hats != nullptr &&
                            remap.hatIndex < snap.hatCount)
                               ? snap.hats[remap.hatIndex]
                               : hat::kCentered;
    if ((h & hat::kUp) != 0) { btn |= B::kDpadUp; }
    if ((h & hat::kDown) != 0) { btn |= B::kDpadDown; }
    if ((h & hat::kLeft) != 0) { btn |= B::kDpadLeft; }
    if ((h & hat::kRight) != 0) { btn |= B::kDpadRight; }

    st.wButtons = btn;
    return st;
}

GamepadInputProcessor::DeviceState mapJoystick(const JoystickSnapshot& snap) {
    return mapJoystick(snap, JoystickRemap{});
}

} // namespace dish::input
