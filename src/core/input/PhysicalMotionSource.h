// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PhysicalMotionSource — the PURE, host-testable core of the physical-controller
// motion path, ported from the platform-independent logic of dish-android
// source/sensor/PhysicalMotionSource.kt + PhysicalMotionProbe.kt. The reactive
// glue (SDL sensor registration, lifecycle) stays on the SDL bridge; everything
// decision-shaped lives here as free functions so it unit-tests without SDL:
//
//   * convertControllerSample — gyro rad/s -> wire int16 with IDENTITY axes
//     (a controller's IMU body frame already matches the wire convention, so —
//     unlike the phone path — there is NO landscape remap). Accel is already
//     scaled by the caller and passes through verbatim.
//   * shouldEmitGyro — gate the first gyro emission until accel has reported,
//     so a pad with an accelerometer never ships a (0,0,0) accel read as
//     genuine zero-gravity. A pad with no accel sensor emits immediately.
//   * filterByCapability — keep only the reachable slots whose pad has a gyro
//     AND the user has motion enabled (an unknown slot is treated as no-motion,
//     the safe default during the startup race).
//   * MotionProbe::hasGyro — the per-device gyro-availability predicate,
//     re-derived from android's API-31 InputDevice.sensorManager probe as the
//     SDL "controller reports a gyro sensor" boolean.
//
// Qt-free; depends only on the wire-scale helpers in Input/SdlMotionConvert.h
// (themselves pure C++).

#pragma once

#include "Input/SdlMotionConvert.h"

#include <cstdint>
#include <map>
#include <string>

namespace dish::input::motion {

// One converted IMU sample destined for MSG_MOTION (the same six int16 fields
// GamepadInputProcessor::MotionSample carries).
struct MotionSample {
    std::int16_t gyroX = 0;
    std::int16_t gyroY = 0;
    std::int16_t gyroZ = 0;
    std::int16_t accelX = 0;
    std::int16_t accelY = 0;
    std::int16_t accelZ = 0;

    bool operator==(const MotionSample& o) const {
        return gyroX == o.gyroX && gyroY == o.gyroY && gyroZ == o.gyroZ && accelX == o.accelX &&
               accelY == o.accelY && accelZ == o.accelZ;
    }
};

// Convert a controller gyro sample (rad/s, body frame) to the wire int16 with
// IDENTITY axes (no remap). Accel is supplied already-scaled (the caller scaled
// the m/s^2 reading via accelMps2ToInt16) and passes through unchanged.
inline MotionSample convertControllerSample(float gyroX, float gyroY, float gyroZ,
                                            std::int16_t accelX, std::int16_t accelY,
                                            std::int16_t accelZ) {
    MotionSample s;
    s.gyroX = gyroRadPerSecToInt16(gyroX);
    s.gyroY = gyroRadPerSecToInt16(gyroY);
    s.gyroZ = gyroRadPerSecToInt16(gyroZ);
    s.accelX = accelX;
    s.accelY = accelY;
    s.accelZ = accelZ;
    return s;
}

// Whether a gyro sample should be emitted yet. A pad with no accelerometer emits
// immediately (the alternative is an indefinitely silent gyro stream waiting on
// an accel reading that never comes); a pad WITH an accelerometer waits until
// the first accel sample has landed (`accelSeen`).
inline bool shouldEmitGyro(bool hasAccelSensor, bool accelSeen) {
    return !hasAccelSensor || accelSeen;
}

// What `filterByCapability` needs from a per-slot capability: a gyro present and
// the user toggle on. (Mirrors the two fields android reads off MotionCapability;
// kept as a tiny local struct so core/input doesn't depend on the composer.)
struct MotionGate {
    bool hasGyro = false;
    bool userEnabled = false;
};

// Keep only the reachable slots that have a gyro AND are user-enabled. `Conn` is
// the opaque per-slot connection handle (the SatelliteConnection on android, a
// pointer/id here) — templated so core/input stays decoupled from the connection
// layer. A slot missing from `gates` is dropped (unknown => no-motion, safe).
template <class Conn>
std::map<std::string, Conn> filterByCapability(const std::map<std::string, Conn>& reachable,
                                               const std::map<std::string, MotionGate>& gates) {
    std::map<std::string, Conn> out;
    for (const auto& [slotId, conn] : reachable) {
        const auto it = gates.find(slotId);
        if (it == gates.end()) { continue; }
        if (it->second.hasGyro && it->second.userEnabled) { out.emplace(slotId, conn); }
    }
    return out;
}

// Per-device gyro-availability probe. On android this is the API-31
// InputDevice.sensorManager.getDefaultSensor(GYROSCOPE) != null check; on
// Windows the SDL bridge supplies the same fact via SDL_GameControllerHasSensor.
// `sensorApiAvailable` mirrors the API-31 gate (always true on Windows/SDL2 —
// pass true), `deviceHasGyro` is the per-device sensor presence.
inline bool probeHasGyro(bool sensorApiAvailable, bool deviceHasGyro) {
    return sensorApiAvailable && deviceHasGyro;
}

} // namespace dish::input::motion
