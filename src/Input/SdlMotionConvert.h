// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <cstdint>

namespace dish::input {

// SDL → wire conversion helpers for the motion (gyro / accel) and touchpad
// surfaces. Extracted out of SDLGamepadBridge.cpp's anonymous namespace so the
// arithmetic can be unit-tested without bringing up SDL. The wire scale
// matches satellite/src/core/types.h.
//
//   gyro:  SDL gives rad/s; convert to deg/s, then to int16 LSB = 2000/32767
//   accel: SDL gives m/s²;  convert to g (÷ 9.80665), then to int16 LSB = 4/32767
//
// All three helpers clamp to the int16 range, so input outside the
// representable span saturates rather than wrapping.

// Convert an angular velocity in radians/second to the wire int16. Full scale
// is ±2000 deg/s → ±32767.
std::int16_t gyroRadPerSecToInt16(float radPerSec);

// Convert a linear acceleration in metres/second² to the wire int16. Full
// scale is ±4 g → ±32767 (1 g ≈ 8192).
std::int16_t accelMps2ToInt16(float mps2);

// Convert an SDL touchpad coordinate (0..1, top-left origin) to a
// resolution-independent signed int16 spanning the pad: 0 → -32768, 1 → +32767.
std::int16_t touchpadCoordToInt16(float v);

} // namespace dish::input
