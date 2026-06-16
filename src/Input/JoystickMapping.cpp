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

GamepadInputProcessor::DeviceState mapJoystick(const JoystickSnapshot& snap) {
    using B = GamepadInputProcessor::Buttons;
    GamepadInputProcessor::DeviceState st{};

    const bool triggerAxes = hasTriggerAxes(snap);

    // ── Sticks ──────────────────────────────────────────────────────────────
    // Left stick is axes 0/1 in every common generic layout. Right stick is
    // axes 3/4 when dedicated trigger axes are present (6-axis pads put the
    // triggers on 2/5), else axes 2/3.
    st.lx = axisAt(snap, 0);
    st.ly = static_cast<std::int16_t>(-axisAt(snap, 1)); // SDL +down → report +up
    const int rightX = triggerAxes ? 3 : 2;
    const int rightY = triggerAxes ? 4 : 3;
    st.rx = axisAt(snap, rightX);
    st.ry = static_cast<std::int16_t>(-axisAt(snap, rightY));

    // ── Buttons ─────────────────────────────────────────────────────────────
    // Face buttons 0-3 map A/B/X/Y (south/east/west/north) in DirectInput
    // order; stick clicks are the high indices.
    std::uint16_t btn = 0;
    if (buttonAt(snap, 0)) { btn |= B::kA; }
    if (buttonAt(snap, 1)) { btn |= B::kB; }
    if (buttonAt(snap, 2)) { btn |= B::kX; }
    if (buttonAt(snap, 3)) { btn |= B::kY; }
    if (buttonAt(snap, 4)) { btn |= B::kLeftShoulder; }
    if (buttonAt(snap, 5)) { btn |= B::kRightShoulder; }
    if (buttonAt(snap, 6)) { btn |= B::kBack; }
    if (buttonAt(snap, 7)) { btn |= B::kStart; }
    // Button 8 is the guide/home key on pads that have one. The XUSB report the
    // processor consumes has no guide bit (see GamepadInputProcessor::Buttons),
    // so it is intentionally dropped here rather than aliased onto Start/Back.
    if (buttonAt(snap, 10)) { btn |= B::kLeftThumb; }
    if (buttonAt(snap, 11)) { btn |= B::kRightThumb; }

    // ── Triggers ────────────────────────────────────────────────────────────
    if (triggerAxes) {
        st.lt = triggerFromAxis(axisAt(snap, 2));
        st.rt = triggerFromAxis(axisAt(snap, 5));
    } else {
        // No dedicated trigger axes: the two trigger keys are the next free
        // buttons after the shoulders. Many 4-axis pads expose L2/R2 as
        // buttons 6/7 — but those are already claimed for back/start above on
        // pads that have a select/start cluster. A generic pad cannot have it
        // both ways from a static default; we treat 6/7 as back/start (the more
        // common case) and source triggers from buttons 8/9 so a press still
        // produces a full-scale trigger. This is exactly the kind of ambiguity
        // a per-device remap resolves.
        st.lt = buttonAt(snap, 8) ? 255 : 0;
        st.rt = buttonAt(snap, 9) ? 255 : 0;
    }

    // ── Hat → dpad ──────────────────────────────────────────────────────────
    // Hat 0 only; SDL_HAT_* is a bitmask so diagonals (e.g. up-right) set two
    // dpad bits. A pad with no hat leaves the dpad clear.
    const std::uint8_t h = (snap.hats != nullptr && snap.hatCount > 0) ? snap.hats[0] : hat::kCentered;
    if ((h & hat::kUp) != 0) { btn |= B::kDpadUp; }
    if ((h & hat::kDown) != 0) { btn |= B::kDpadDown; }
    if ((h & hat::kLeft) != 0) { btn |= B::kDpadLeft; }
    if ((h & hat::kRight) != 0) { btn |= B::kDpadRight; }

    st.wButtons = btn;

    // TODO(remap): a future per-device remap UI plugs in here — it would take
    // the same JoystickSnapshot and a stored per-(vid,pid) remap table and
    // override the axis/button routing above. The default layout is a best
    // guess; the remap is where a user corrects it for their specific pad.

    return st;
}

} // namespace dish::input
