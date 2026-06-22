// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Deadzones — the pure, Qt-free deadzone + axis/trigger scaling math, lifted out
// of the hot-path GamepadInputProcessor so it can be host-tested without the
// processor's lock plumbing. Mirrors dish-android's per-device `flat` pipeline
// (the value Android pulls from `InputDevice.getMotionRange(axis).getFlat()`);
// SDL2 has no OS-level equivalent, so the bridge installs a per-device profile.
//
// The rule (identical across all Dish clients, the wire contract):
//   * sticks:   |v| <= stickFlat  -> 0
//   * triggers:  v  <= triggerFlat -> 0   (triggers are unsigned 0..255)
//   * buttons pass through untouched.
//
// Free functions operating on plain scalars (no Qt, no SDL, no processor struct)
// so the arithmetic is unit-testable in isolation; GamepadInputProcessor calls
// these from its publish() path. No allocation — all by value.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace dish::input::deadzone {

// Per-device deadzone thresholds. `stickFlat` is on the int16 stick scale;
// `triggerFlat` is on the 0..255 trigger scale. The default profile the bridge
// installs is ~10 % stick / ~5 % trigger (3277 / 13).
struct Deadzones {
    std::int16_t stickFlat = 0;
    std::uint8_t triggerFlat = 0;

    bool operator==(const Deadzones& o) const {
        return stickFlat == o.stickFlat && triggerFlat == o.triggerFlat;
    }
    bool operator!=(const Deadzones& o) const { return !(*this == o); }
};

// Apply the stick deadzone to one signed axis value: zero it iff its magnitude
// is at or below `flat`. Computed in int32 so std::abs(INT16_MIN) does not
// overflow.
inline std::int16_t applyStick(std::int16_t v, std::int16_t flat) {
    if (std::abs(static_cast<std::int32_t>(v)) <= static_cast<std::int32_t>(flat)) { return 0; }
    return v;
}

// Apply the trigger deadzone to one unsigned trigger value: zero it iff it is at
// or below `flat`.
inline std::uint8_t applyTrigger(std::uint8_t v, std::uint8_t flat) { return v <= flat ? 0 : v; }

// Scale a normalized [-1, 1] axis to the int16 wire range, clamping the input
// first so out-of-range values saturate rather than wrap. `maxMagnitude` is the
// positive full-scale value (typically 32767).
inline std::int16_t scaleAxis(float v, float maxMagnitude) {
    const float clamped = std::clamp(v, -1.0F, 1.0F);
    const int scaled = static_cast<int>(clamped * maxMagnitude);
    return static_cast<std::int16_t>(
        std::clamp(scaled, static_cast<int>(INT16_MIN), static_cast<int>(INT16_MAX)));
}

// Scale a normalized [0, 1] trigger to the 0..255 wire range (even rounding,
// clamped). Input is clamped to [0, 1] first.
inline std::uint8_t scaleTrigger(float v) {
    const float clamped = std::clamp(v, 0.0F, 1.0F);
    const int scaled = static_cast<int>(std::lround(clamped * 255.0F));
    return static_cast<std::uint8_t>(std::clamp(scaled, 0, 255));
}

} // namespace dish::input::deadzone
