// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MotionIndicatorState — the pure precedence mapper behind the per-slot motion
// indicator the dashboard renders (the little gyro chip on a slot card). Given
// the slot's motion facts it folds them down to exactly one user-facing state
// with a strict precedence: the most "blocking" reason wins so the user sees the
// single most-actionable explanation rather than a pile of overlapping warnings.
//
// Mirrors dish-android ui/main/MotionIndicatorState (motionIndicatorFor) — a
// pure function. Qt-free, no allocation, no IO: it reads the values that 2d's
// MotionEnabledStore / SatelliteMotionBackendStatusStore and 2b's session state
// expose and returns an enum; the UI/mapper turns the enum into a localized
// label + glyph (no localized text leaks into this header).
//
// Precedence ladder (first matching rule wins):
//   UNAVAILABLE  > USER_DISABLED > NOT_FORWARDED > NO_HOST_SINK >
//   BACKEND_BROKEN > STALLED > STREAMING / PAUSED
//
//   UNAVAILABLE    — the pad has no gyro (nothing to forward, ever).
//   USER_DISABLED  — pad has a gyro but the user toggled motion off.
//   NOT_FORWARDED  — enabled, but the slot isn't on a live link that carries it.
//   NO_HOST_SINK   — forwarded, but the host has no motion sink for this
//                    controller type (e.g. an Xbox-typed slot; the satellite
//                    only sinks motion for PlayStation-typed slots).
//   BACKEND_BROKEN — the host has a sink but its motion backend reports unhealthy.
//   STALLED        — everything is wired but no motion samples are arriving
//                    (the gyro went quiet / the stream stalled).
//   STREAMING      — motion is actively flowing.
//   PAUSED         — wired and live but intentionally paused (the fallback when
//                    not actively streaming and not stalled).

#pragma once

namespace dish::reducer {

// The user-facing motion indicator state. Order is the precedence order; do not
// reorder without updating motionIndicatorFor and the test table.
enum class MotionIndicatorState {
    Unavailable,   // no gyro hardware
    UserDisabled,  // user turned motion off
    NotForwarded,  // not on a live link that carries motion
    NoHostSink,    // host has no sink for this controller type
    BackendBroken, // host sink present but motion backend unhealthy
    Stalled,       // wired+live but no samples arriving
    Streaming,     // motion actively flowing
    Paused,        // wired+live, intentionally paused (fallback)
};

// The facts the mapper folds. Each is read once from a snapshot off the hot
// path; the mapper itself is pure.
//
//  * hasGyro            — SDL reported a gyro for the pad (HasSensor).
//  * userEnabled        — the per-slot user motion toggle (default on).
//  * carriesOnConnection— the slot is bound to a live satellite link that
//                         carries motion (the local emit gate).
//  * hostHasSinkForType — the host advertises a motion sink for the slot's
//                         controller type (PlayStation-typed only).
//  * backendOk          — the satellite's reported motion backend health. Only
//                         consulted once hostHasSinkForType holds.
//  * isStreaming        — motion samples are actively arriving for the slot.
//  * isPaused           — motion is intentionally paused (vs. silently stalled).
struct MotionIndicatorInputs {
    bool hasGyro = false;
    bool userEnabled = true;
    bool carriesOnConnection = false;
    bool hostHasSinkForType = true;
    bool backendOk = true;
    bool isStreaming = false;
    bool isPaused = false;
};

// Fold the inputs to exactly one state via the strict precedence ladder above.
// Pure; total (every input combination returns a defined state).
inline MotionIndicatorState motionIndicatorFor(const MotionIndicatorInputs& in) {
    if (!in.hasGyro) { return MotionIndicatorState::Unavailable; }
    if (!in.userEnabled) { return MotionIndicatorState::UserDisabled; }
    if (!in.carriesOnConnection) { return MotionIndicatorState::NotForwarded; }
    if (!in.hostHasSinkForType) { return MotionIndicatorState::NoHostSink; }
    if (!in.backendOk) { return MotionIndicatorState::BackendBroken; }
    if (in.isStreaming) { return MotionIndicatorState::Streaming; }
    if (in.isPaused) { return MotionIndicatorState::Paused; }
    // Wired and live, neither actively streaming nor flagged paused: treat the
    // quiet gyro as a stall so the user knows samples aren't landing.
    return MotionIndicatorState::Stalled;
}

// ── Meter-visibility conjunctions ────────────────────────────────────────────

// Whether the per-slot motion-rate meter (the live Hz readout) should be shown.
// Mirrors android ui/main/motionRateUserFacingOn: only for a physical pad that
// actually has a motion capability and isn't in the "no gyro hardware" state —
// every other indicator state (disabled, not-forwarded, broken, streaming…)
// still shows the meter so the user can see it sitting at 0 Hz with the reason.
// (The android "virtual slot" arm is dropped — Windows has physical pads only.)
inline bool motionRateUserFacingOn(bool hasMotionCapability, MotionIndicatorState state) {
    return hasMotionCapability && state != MotionIndicatorState::Unavailable;
}

// Whether the per-slot touchpad ("screen") rate meter should be shown. Android's
// rule is "virtual slot always (while connected); a physical slot iff the
// satellite link is up AND touchpad forwarding is on." The virtual-slot arm is
// phone-only and dropped; the physical arm is what Windows keeps: a real
// DualSense/DS4 touchpad's rate is meaningful only when the link is live and the
// user has touchpad forwarding enabled.
inline bool screenRateUserFacingOn(bool satelliteConnected, bool touchpadForwardingOn) {
    return satelliteConnected && touchpadForwardingOn;
}

} // namespace dish::reducer
