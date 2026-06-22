// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure touchpad-routing decision for the MSG_TOUCHPAD (0x000C) forward path
// (Workstream 2e). Free functions, Qt-free.
//
// dish-windows forwards a REAL controller's touchpad (DualSense / DS4 two-finger
// pad) — it is AHEAD of android, which only forwards an on-screen surface. The
// android on-screen TouchpadPadCoordinator / TouchpadState surface tests are
// phone-only and SKIP; the routing pinned here is the physical-pad forward.
//
// Two pure pieces live here:
//   1. The forward payload assembly — fold the assembled two-finger SDL sample
//      plus a monotonic eventTimeMs into the value handed to the wire encoder
//      (SatelliteClient::encodeTouchpadPayload, owned by Wave 1, already 16
//      bytes with the trailing eventTimeMs). The eventTimeMs plumbing is the
//      2e routing fix: a fresh per-publish uptime-ms stamp satisfies the
//      protocol-1 requirement so the satellite no longer drops the packet
//      (it needs msgLen >= 16 inner; a 12-byte body was silently dropped).
//   2. The host-feature decision: D2 ships v1 with mouseControl = false (no
//      mouse mode). The descriptor that actually carries the flag is sent by
//      Workstream 2b; this constant pins the v1 value the routing assumes.
//
// Touchpad input is genuinely event-driven (finger down/move/up) and is neither
// rate-limited nor coalesced — every assembled state change is forwarded. So the
// routing has no "should I forward?" gate beyond "a sample arrived"; the
// decision worth unit-pinning is the payload assembly + eventTimeMs carry.

#pragma once

#include <cstdint>

namespace dish::reducer {

// D2: dish-windows ships v1 with mouse-control mode OFF. The DS4 touchpad is
// forwarded as a touchpad (touchpadMode ds4), never as a mouse. The session
// descriptor carries hostFeatures.mouseControl = this value (sent by 2b).
inline constexpr bool kTouchpadMouseControlV1 = false;

// The assembled two-finger touchpad state plus the sender-side timestamp — the
// exact set of values the wire encoder consumes. Mirrors the encoder's argument
// list (SatelliteClient::encodeTouchpadPayload) so the routing→encoder seam is
// a single value, and `eventTimeMs` is visibly threaded through (the 2e fix).
struct TouchpadForward {
    bool finger0Active = false;
    std::uint8_t finger0Id = 0;
    std::int16_t finger0X = 0;
    std::int16_t finger0Y = 0;
    bool finger1Active = false;
    std::uint8_t finger1Id = 0;
    std::int16_t finger1X = 0;
    std::int16_t finger1Y = 0;
    bool buttonPressed = false;
    // Protocol-1 trailing field (u32 LE on the wire). The satellite's
    // decodeTouchpadReport reads le32(p + 11); a missing/short field made the
    // server drop the whole packet pre-fix. A monotonic uptime-ms stamp per
    // publish matches the contract's eventTimeMs (mouse-mode timing scales by
    // the delta between consecutive samples).
    std::uint32_t eventTimeMs = 0;

    bool operator==(const TouchpadForward& o) const {
        return finger0Active == o.finger0Active && finger0Id == o.finger0Id &&
               finger0X == o.finger0X && finger0Y == o.finger0Y &&
               finger1Active == o.finger1Active && finger1Id == o.finger1Id &&
               finger1X == o.finger1X && finger1Y == o.finger1Y &&
               buttonPressed == o.buttonPressed && eventTimeMs == o.eventTimeMs;
    }
};

// Assemble the forward payload from the per-finger fields + the monotonic
// timestamp. Pure: it does not zero an inactive finger's id/coords (the active
// flags are the sole source of truth, matching the encoder's pure-layout
// contract). The whole point is that `eventTimeMs` is carried end-to-end — a
// regression that drops it would re-introduce the silent server-side drop.
inline TouchpadForward assembleTouchpadForward(bool finger0Active, std::uint8_t finger0Id,
                                               std::int16_t finger0X, std::int16_t finger0Y,
                                               bool finger1Active, std::uint8_t finger1Id,
                                               std::int16_t finger1X, std::int16_t finger1Y,
                                               bool buttonPressed, std::uint32_t eventTimeMs) {
    TouchpadForward f;
    f.finger0Active = finger0Active;
    f.finger0Id = finger0Id;
    f.finger0X = finger0X;
    f.finger0Y = finger0Y;
    f.finger1Active = finger1Active;
    f.finger1Id = finger1Id;
    f.finger1X = finger1X;
    f.finger1Y = finger1Y;
    f.buttonPressed = buttonPressed;
    f.eventTimeMs = eventTimeMs;
    return f;
}

} // namespace dish::reducer
