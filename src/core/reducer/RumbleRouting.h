// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure rumble-routing decision logic for the MSG_RUMBLE (0x0009) return path
// (Workstream 2e). Free functions, Qt-free, socket-free — the analogue of
// dish-android hotpath/input/RumbleRouter.kt's pure surface (resolveRumble /
// resolveSlotId / classifyTarget / combinedRumblePlan / rumbleMagnitudeTo255 /
// rumbleSafeDurationMs / isRumbleStop).
//
// The receive thread snapshots the live connection→slot bindings ONCE into an
// immutable view and decides with these pure functions, so the native receive
// thread never re-reads live state mid-resolve (android-architecture.md: "reads
// each StateFlow once into an immutable snapshot and decides via the pure
// resolveRumble"). The actuation itself still runs on the SDL thread via
// OutputCommandQueue — this header decides WHAT to actuate, not HOW.
//
// Platform deltas vs android (intentional, per the 2e spec):
//   * The Phone-vibrator arm (VIRTUAL_SLOT_ID → RumbleTarget.Phone) is DROPPED:
//     Windows is physical-controllers-only, there is no on-screen pad.
//   * The DirectUsb arm (negative synthetic slot id → RumbleTarget.DirectUsb) is
//     DROPPED: D1 moved the USB-direct subsystem out of this slice (2g).
//   * Android keys connection→slot by an integer sessionHandle + controllerIndex
//     into a slot map; Windows keys by the connection id string and resolves to
//     the bound SDL device id (on Windows slot.id == the SDL bridge device id).
//     The pure shape — "snapshot once, resolve to a target, drop the non-
//     physical arms" — is preserved; the math helpers are ported byte-for-byte.

#pragma once

#include <QString>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace dish::reducer {

// 16-bit magnitude → 8-bit (SDL/XInput rumble is 16-bit; the satellite sends a
// 16-bit magnitude on the wire too, but the helper is the canonical scaler the
// android RumbleBridgeHelpers pins). Returns 0 only for exact zero; any tiny
// non-zero magnitude clamps UP to 1 so an on/off response matches a physical
// pad (an imperceptible buzz must never become silent). Even-rounding via the
// +32767 bias; out-of-range inputs are clamped before scaling.
//
// Ported from RumbleRouter.kt::rumbleMagnitudeTo255:
//   val clamped = magnitude.coerceIn(0, 65535)
//   if (clamped == 0) return 0
//   val scaled = (clamped * 255 + 32767) / 65535
//   return scaled.coerceIn(1, 255)
inline int rumbleMagnitudeTo255(int magnitude) {
    const int clamped = std::clamp(magnitude, 0, 65535);
    if (clamped == 0) { return 0; }
    const int scaled = (clamped * 255 + 32767) / 65535;
    return std::clamp(scaled, 1, 255);
}

// Clamp a rumble duration into the safe window. 0 is preserved as the stop
// sentinel; a cap of 1500 ms stops a buggy/malicious satellite stranding a
// multi-second buzz on the pad; a negative duration is treated as the minimum
// (1 ms). Ported from RumbleRouter.kt::rumbleSafeDurationMs:
//   if (durationMs == 0) return 0
//   return durationMs.coerceIn(1, 1500)
inline int rumbleSafeDurationMs(int durationMs) {
    if (durationMs == 0) { return 0; }
    return std::clamp(durationMs, 1, 1500);
}

// A rumble request is a STOP/cancel (never a positive vibration) when the
// duration is zero OR both magnitudes are zero. Ported from
// RumbleRouter.kt::isRumbleStop.
inline bool isRumbleStop(int strongMagnitude, int weakMagnitude, int durationMs) {
    return durationMs == 0 || (strongMagnitude == 0 && weakMagnitude == 0);
}

// One actuator command: (actuatorIndex, amplitude).
using RumbleActuator = std::pair<int, int>;

// Split a strong/weak rumble across the pad's actuators. A two-actuator target
// separates strong→index 0 / weak→index 1 (dropping a zero amplitude so an
// empty combination is never submitted); a single actuator folds to
// max(strong, weak) so a weak-only effect is still felt; zero actuators (or a
// fully-zero amplitude pair) yields nothing. Ported from
// RumbleRouter.kt::combinedRumblePlan.
inline std::vector<RumbleActuator> combinedRumblePlan(int vibratorCount, int strongAmp,
                                                      int weakAmp) {
    std::vector<RumbleActuator> out;
    if (vibratorCount <= 0) { return out; }
    if (vibratorCount >= 2) {
        if (strongAmp > 0) { out.emplace_back(0, strongAmp); }
        if (weakAmp > 0) { out.emplace_back(1, weakAmp); }
        return out;
    }
    const int amp = std::max(strongAmp, weakAmp);
    if (amp > 0) { out.emplace_back(0, amp); }
    return out;
}

// ── Windows target resolution (the physical-only arm of android's classify) ──
//
// Flat, immutable view of one connection captured once per dispatch so
// resolveRumble stays pure. `connId` is the WifiConnection id the receive-thread
// rumble handler was installed for; `connected` is whether the session is live;
// `boundDeviceId` is the SDL bridge device id bound to this connection (empty
// when nothing is bound). The android equivalent is RumbleConnectionSnapshot
// (handle / connected / slots-map); on Windows the slot resolves directly to the
// device id, so we fold resolveSlotId+classifyTarget into the bound device id.
struct RumbleConnectionSnapshot {
    QString connId;
    bool connected = false;
    QString boundDeviceId;
};

// The resolved rumble destination: the SDL bridge device id to actuate, or empty
// when nothing should be driven (no matching connection / nothing bound). This
// is android's RumbleTarget collapsed to its only surviving (Framework) arm —
// Phone and DirectUsb are dropped on Windows.
struct RumbleTarget {
    QString deviceId; // empty == RumbleTarget.None

    bool valid() const { return !deviceId.isEmpty(); }
    bool operator==(const RumbleTarget& o) const { return deviceId == o.deviceId; }
};

// Resolve the device to actuate for a rumble that arrived on `connId`'s session.
// Collision rule (ported from resolveRumble): a connected match wins over a
// non-connected one with the same id so a stale session can't steal a live
// controller's rumble; among equally-connected matches the first in snapshot
// order wins (deterministic). An empty `connId`, no match, or a matched-but-
// unbound connection all yield RumbleTarget.None (empty deviceId).
inline RumbleTarget resolveRumble(const std::vector<RumbleConnectionSnapshot>& connections,
                                  const QString& connId) {
    if (connId.isEmpty()) { return {}; }
    const RumbleConnectionSnapshot* chosen = nullptr;
    for (const auto& c : connections) {
        if (c.connId != connId) { continue; }
        if (c.connected) {
            chosen = &c;
            break;
        }
        if (chosen == nullptr) { chosen = &c; }
    }
    if (chosen == nullptr) { return {}; }
    if (chosen->boundDeviceId.isEmpty()) { return {}; }
    return RumbleTarget{chosen->boundDeviceId};
}

} // namespace dish::reducer
