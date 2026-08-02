// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CapabilitySolver — the single owner of the four-layer capability vocabulary
// shared by the wizard's type table and Configure binding's WHAT CARRIES
// matrix. Two surfaces used to derive this independently; that is exactly how
// they drift.
//
//     available = controller n transport n type n host
//
//   * Input  — what the pad itself reports (gyro / touchpad / rumble / lightbar)
//   * Link   — the USB path: Direct carries everything, Standard carries the
//              gamepad, its triggers and rumble
//   * Type   — the catalog type's `features` (an Xbox 360 type carries no gyro
//              however good the pad is)
//   * Host   — the satellite's `hostFeatures` (mouse control, rumble return);
//              a Bluetooth host is Windows' own gamepad layer and carries only
//              the gamepad, its triggers and rumble
//
// Four verdicts, and the distinctions between them are the whole point:
//   Available   — every layer carries it and the user has it on
//   Unavailable — a layer refuses it; `failingLayer` names the FIRST one, which
//                 is the one whose fix is actionable
//   Off         — every layer carries it and the USER switched it off
//   Pending     — the host or its catalog has not resolved. Never a guess: an
//                 unread layer NEVER refuses, so a `x` is never drawn from an
//                 unresolved catalog.
//
// Pure, Qt-free, header-only: the vocabulary must be testable without a
// satellite, a catalog fetch or a QML engine. Localised copy lives in QML — the
// solver returns tokens only.

#pragma once

#include <vector>

namespace dish::reducer {

enum class CapLayer { Input, Link, Type, Host };
enum class CapVerdict { Available, Unavailable, Pending, Off };

// The seven features, in the fixed render order both surfaces use.
enum class CapFeature { Gamepad, Triggers, Motion, Touchpad, Mouse, Rumble, Lightbar };

struct CapabilityInputs {
    // Input layer — what the pad itself reports.
    bool padMotion = false;
    bool padTouchpad = false;
    bool padRumble = true; // every supported pad has motors today
    bool padLightbar = false;
    // Link layer.
    bool linkDirect = false;   // usb && directCapable && draft wants Direct
    bool linkUsb = false;      // false => Bluetooth transport
    bool padClaimable = false; // pathSupported
    // Type layer. `typeResolved == false` => the type layer refuses nothing.
    bool typeResolved = false;
    bool typeMotion = false, typeTouchpad = false, typeRumble = false, typeLightbar = false;
    // Host layer. `hostResolved == false` => Pending.
    bool hostResolved = false;
    bool hostIsBluetooth = false;
    bool hostMouseControl = false;
    bool hostRumble = false;
    // User gates (drive `Off`, never `Unavailable`).
    bool userMotionOn = true, userRumbleOn = true;
    int userTouchpadMode = 0; // 0=off 1=pad 2=mouse
};

struct CapabilityRow {
    CapFeature feature{};
    bool inOk = false, linkOk = false, typeOk = false, hostOk = false;
    CapVerdict verdict = CapVerdict::Pending;
    CapLayer failingLayer = CapLayer::Input; // meaningful iff verdict == Unavailable
    bool hasFailingLayer = false;
};

namespace detail {

// `linkUsb` / `padClaimable` do not change the transport SET (only `linkDirect`
// does); they are carried so the caller can pick the right REASON — "switch to
// Direct" for a claimable USB pad vs "Direct needs a USB connection" over
// Bluetooth. The solver never vends a sentence.
inline bool inputCarries(const CapabilityInputs& in, CapFeature f) {
    switch (f) {
    case CapFeature::Gamepad:
    case CapFeature::Triggers:
        return true;
    case CapFeature::Motion:
        return in.padMotion;
    case CapFeature::Touchpad:
        return in.padTouchpad;
    // Mouse is a ROUTING of the touchpad, so the pad needs one to drive it.
    case CapFeature::Mouse:
        return in.padTouchpad;
    case CapFeature::Rumble:
        return in.padRumble;
    case CapFeature::Lightbar:
        return in.padLightbar;
    }
    return false;
}

inline bool linkCarries(const CapabilityInputs& in, CapFeature f) {
    if (in.linkDirect) { return true; } // a raw-HID claim carries every feature
    switch (f) {
    case CapFeature::Gamepad:
    case CapFeature::Triggers:
    case CapFeature::Rumble:
        return true;
    default:
        return false;
    }
}

inline bool typeCarries(const CapabilityInputs& in, CapFeature f) {
    // An unread catalog refuses nothing — a guessed "unsupported" is worse than
    // no table. The Pending verdict below is what tells the user we don't know.
    if (!in.typeResolved) { return true; }
    switch (f) {
    case CapFeature::Gamepad:
    case CapFeature::Triggers:
        return true;
    case CapFeature::Motion:
        return in.typeMotion;
    case CapFeature::Touchpad:
        return in.typeTouchpad;
    // Mouse is a routing of the touchpad, not a catalog type feature.
    case CapFeature::Mouse:
        return true;
    case CapFeature::Rumble:
        return in.typeRumble;
    case CapFeature::Lightbar:
        return in.typeLightbar;
    }
    return false;
}

inline bool hostCarries(const CapabilityInputs& in, CapFeature f) {
    if (!in.hostResolved) { return true; } // same rule as the type layer
    if (in.hostIsBluetooth) {
        // Windows' own gamepad layer: no motion channel, no touch channel, no
        // lightbar return.
        return f == CapFeature::Gamepad || f == CapFeature::Triggers || f == CapFeature::Rumble;
    }
    switch (f) {
    case CapFeature::Gamepad:
    case CapFeature::Triggers:
    case CapFeature::Motion:
    case CapFeature::Touchpad:
    case CapFeature::Lightbar:
        return true;
    case CapFeature::Mouse:
        return in.hostMouseControl;
    case CapFeature::Rumble:
        return in.hostRumble;
    }
    return false;
}

// Whether the USER has this feature switched on. Only reachable when all four
// layers carry it, so it can never masquerade as "unsupported".
inline bool userEnabled(const CapabilityInputs& in, CapFeature f) {
    switch (f) {
    case CapFeature::Motion:
        return in.userMotionOn;
    case CapFeature::Rumble:
        return in.userRumbleOn;
    case CapFeature::Touchpad:
        return in.userTouchpadMode == 1;
    case CapFeature::Mouse:
        return in.userTouchpadMode == 2;
    default:
        return true;
    }
}

} // namespace detail

// True while the host, or the catalog that describes what a type carries, has
// not resolved. A Bluetooth host needs no catalog — it is not a satellite and
// offers no types — so only a satellite host waits on one.
inline bool capabilitiesPending(const CapabilityInputs& in) {
    return !in.hostResolved || (!in.hostIsBluetooth && !in.typeResolved);
}

inline std::vector<CapabilityRow> solveCapabilities(const CapabilityInputs& in) {
    static constexpr CapFeature kOrder[] = {
        CapFeature::Gamepad, CapFeature::Triggers, CapFeature::Motion,  CapFeature::Touchpad,
        CapFeature::Mouse,   CapFeature::Rumble,   CapFeature::Lightbar};
    const bool pending = capabilitiesPending(in);

    std::vector<CapabilityRow> rows;
    rows.reserve(sizeof(kOrder) / sizeof(kOrder[0]));
    for (const CapFeature f : kOrder) {
        CapabilityRow row;
        row.feature = f;
        row.inOk = detail::inputCarries(in, f);
        row.linkOk = detail::linkCarries(in, f);
        row.typeOk = detail::typeCarries(in, f);
        row.hostOk = detail::hostCarries(in, f);

        if (pending) {
            // No blame while unresolved: the table shows a dash, not a cross.
            row.verdict = CapVerdict::Pending;
        } else if (row.inOk && row.linkOk && row.typeOk && row.hostOk) {
            row.verdict = detail::userEnabled(in, f) ? CapVerdict::Available : CapVerdict::Off;
        } else {
            row.verdict = CapVerdict::Unavailable;
            row.hasFailingLayer = true;
            // The FIRST failing layer, in Input -> Link -> Type -> Host order:
            // the one whose fix the user can actually act on.
            row.failingLayer = !row.inOk     ? CapLayer::Input
                               : !row.linkOk ? CapLayer::Link
                               : !row.typeOk ? CapLayer::Type
                                             : CapLayer::Host;
        }
        rows.push_back(row);
    }
    return rows;
}

} // namespace dish::reducer
