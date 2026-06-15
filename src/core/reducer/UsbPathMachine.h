// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbPathMachine — the explicit per-controller USB input-path lifecycle FSM.
// Pure, Qt-free, exhaustively-tested port of dish-android
// source/usb/UsbPathMachine.kt 1:1.
//
// Every (phase x event) pair is TOTAL here: defined for all combinations,
// never throws, so a failed/odd transition can never silently drop a slot the
// way scattered imperative logic could. Effects are returned AS DATA (a vector
// of UsbEffect) — `reduce` performs no IO. The coordinator (the Windows
// UsbGamepadManager in source/usb/) turns world changes into events, runs
// `reduce`, and executes the returned effects against the real subsystems
// (the raw-HID claim driver, the registry, the connection layer).
//
// Kotlin -> C++ shape:
//   * `sealed interface UsbEvent`  -> std::variant<...> (the events below).
//   * `sealed interface UsbEffect` -> std::variant<...> (the effects below).
//   * `data class Reduction`       -> Reduction { std::optional<UsbController>
//                                      next; std::vector<UsbEffect> effects; }.
//     A nullopt `next` means "remove this controller from tracking" (Kotlin's
//     `next == null`).
//   * `when (phase)` / `when (event)` -> std::visit / switch.
//
// This is platform-independent: the DECISION space is identical on android and
// Windows; only the effector (UsbGamepadManager) differs (WinUSB/raw-HID vs the
// android UsbManager). On Windows the "framework" path the FSM returns control
// to is SDL/XInput; the "Direct" path is the raw-HID claim. XInput hides
// Xbox-class pads from raw HID, so Direct mainly serves DualSense/DS4/8BitDo;
// see source/usb/UsbGamepadManager.h for the fallback discussion.

#pragma once

#include "core/reducer/DirectClaimFailure.h"
#include "core/reducer/PathChoice.h"

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace dish::reducer {

enum class UsbPhase {
    Routed,            // Standard: a framework (SDL/XInput) device is present.
    Claiming,          // a direct-mode claim is in flight (loader, toggle disabled).
    Direct,            // claimed and streaming.
    AwaitingFramework, // released or claim-failed; waiting for the framework device to
                       // re-enumerate.
    RestoreStuck, // a return-to-Standard never re-enumerated; user picks Direct, retry, or replug.
    NeedsReplug,  // present but the OS never gave the device back; needs a manual replug.
};

// One USB controller's path state. Keyed by vpKey (vendorId<<16 | productId)
// in the coordinator. Mirrors the android UsbController data class field-for-
// field (defaults match).
struct UsbController {
    int vendorId = 0;
    int productId = 0;
    std::string name;
    UsbPhase phase = UsbPhase::Routed;
    bool usbPresent = true;
    std::optional<int> frameworkId;
    std::optional<int> syntheticId;
    bool hasPermission = false;
    PathChoice desired = PathChoice::Standard;
    // Whether the in-flight switch was an explicit user action (gates user notices).
    bool userInitiated = false;
    // Bound connection, carried with the controller across path switches.
    std::optional<std::string> connId;
    std::optional<int> type;
    // Why the last Direct claim failed; remembered between the failure and the
    // Standard re-settle so the re-enumerated framework card can show the cause.
    std::optional<DirectClaimFailure> failure;

    bool operator==(const UsbController& o) const {
        return vendorId == o.vendorId && productId == o.productId && name == o.name &&
               phase == o.phase && usbPresent == o.usbPresent && frameworkId == o.frameworkId &&
               syntheticId == o.syntheticId && hasPermission == o.hasPermission &&
               desired == o.desired && userInitiated == o.userInitiated && connId == o.connId &&
               type == o.type && failure == o.failure;
    }
    bool operator!=(const UsbController& o) const { return !(*this == o); }
};

// ── Events ────────────────────────────────────────────────────────────────

namespace event {

struct FrameworkUp {
    int id = 0;
    bool operator==(const FrameworkUp& o) const { return id == o.id; }
};
struct FrameworkDown {
    bool operator==(const FrameworkDown&) const { return true; }
};
struct UsbUnplugged {
    bool operator==(const UsbUnplugged&) const { return true; }
};
struct PermissionGranted {
    bool operator==(const PermissionGranted&) const { return true; }
};
struct PermissionDenied {
    bool operator==(const PermissionDenied&) const { return true; }
};
struct Choose {
    PathChoice choice = PathChoice::Standard;
    bool userInitiated = false;
    bool operator==(const Choose& o) const {
        return choice == o.choice && userInitiated == o.userInitiated;
    }
};
struct ClaimSucceeded {
    int syntheticId = 0;
    bool operator==(const ClaimSucceeded& o) const { return syntheticId == o.syntheticId; }
};
// frameworkStolen: the interface was claimed (kernel HID driver detached) before
// the failure, so the framework device must re-enumerate before we can settle on
// Standard. When false the framework was never touched, so the slot is already
// usable on Standard.
struct ClaimFailed {
    DirectClaimFailure reason = DirectClaimFailure::Busy;
    bool frameworkStolen = false;
    bool operator==(const ClaimFailed& o) const {
        return reason == o.reason && frameworkStolen == o.frameworkStolen;
    }
};
struct Timeout {
    bool operator==(const Timeout&) const { return true; }
};

} // namespace event

using UsbEvent = std::variant<event::FrameworkUp, event::FrameworkDown, event::UsbUnplugged,
                              event::PermissionGranted, event::PermissionDenied, event::Choose,
                              event::ClaimSucceeded, event::ClaimFailed, event::Timeout>;

// User-facing banner reasons; the coordinator maps these to localized strings.
enum class UsbNotice {
    SwitchToDirectFailed,
    NeedsReplug,
    RolledBackToDirect,
    RestoreFailed,
};

// ── Effects (returned as data; executed by the coordinator) ─────────────────

namespace effect {

// Coarse: open + claim interface + read-loop attach + register synthetic +
// bind. The coordinator feeds the outcome back as ClaimSucceeded/ClaimFailed.
struct Claim {
    bool operator==(const Claim&) const { return true; }
};
// Coarse rollback: re-claim Direct (known-good), dropping the synthetic
// placeholder. Feeds ClaimSucceeded/ClaimFailed back. Only emitted when the
// user picks Direct out of RestoreStuck.
struct Reclaim {
    bool operator==(const Reclaim&) const { return true; }
};
// Detach the read loop + release interface + keep the synthetic entry as a held
// loader placeholder.
struct Release {
    bool operator==(const Release&) const { return true; }
};
struct RequestPermission {
    bool operator==(const RequestPermission&) const { return true; }
};
// Bind the carried connection to a device id (framework or synthetic).
struct BindFramework {
    int frameworkId = 0;
    bool operator==(const BindFramework& o) const { return frameworkId == o.frameworkId; }
};
struct RemoveSynthetic {
    int syntheticId = 0;
    bool operator==(const RemoveSynthetic& o) const { return syntheticId == o.syntheticId; }
};
// Registry transition hold for the framework device (suppress the grace reaper).
struct BeginHold {
    bool operator==(const BeginHold&) const { return true; }
};
struct EndHold {
    bool operator==(const EndHold&) const { return true; }
};
// Flip the held framework placeholder to a visible "needs replug" card (the OS
// dropped the device and never gave it back), instead of removing it.
struct MarkNeedsReplug {
    bool operator==(const MarkNeedsReplug&) const { return true; }
};
// Flip the held synthetic placeholder to a visible "Standard isn't responding"
// card whose toggle stays live, so the user picks Direct / retry / replug
// instead of the app silently reverting.
struct MarkRestoreStuck {
    bool operator==(const MarkRestoreStuck&) const { return true; }
};
struct ClearRestoreStuck {
    bool operator==(const ClearRestoreStuck&) const { return true; }
};
struct StartTimeout {
    bool operator==(const StartTimeout&) const { return true; }
};
struct Notify {
    UsbNotice notice = UsbNotice::SwitchToDirectFailed;
    bool operator==(const Notify& o) const { return notice == o.notice; }
};
struct SetPref {
    PathChoice choice = PathChoice::Standard;
    bool operator==(const SetPref& o) const { return choice == o.choice; }
};
// Surface why Direct failed on the visible card (and suppress auto-retry for the
// model); cleared whenever a fresh attempt starts or Direct succeeds.
struct MarkFailure {
    DirectClaimFailure reason = DirectClaimFailure::Busy;
    bool operator==(const MarkFailure& o) const { return reason == o.reason; }
};
struct ClearFailure {
    bool operator==(const ClearFailure&) const { return true; }
};

} // namespace effect

using UsbEffect =
    std::variant<effect::Claim, effect::Reclaim, effect::Release, effect::RequestPermission,
                 effect::BindFramework, effect::RemoveSynthetic, effect::BeginHold, effect::EndHold,
                 effect::MarkNeedsReplug, effect::MarkRestoreStuck, effect::ClearRestoreStuck,
                 effect::StartTimeout, effect::Notify, effect::SetPref, effect::MarkFailure,
                 effect::ClearFailure>;

// next == nullopt means "remove this controller from tracking".
struct Reduction {
    std::optional<UsbController> next;
    std::vector<UsbEffect> effects;
};

// The total reducer: (controller, event) -> next state + effects.
Reduction reduce(const UsbController& c, const UsbEvent& event);

// The path-resolution policy, lifted out of the coordinator so each branch is
// checkable directly (UsbPathResolutionTest). An explicit stored pick always
// wins; absent one (stored == nullopt = Auto), auto-Direct ONLY a verified
// fast-lane model that has not just failed to claim. Mirrors android
// resolvePathChoice.
inline PathChoice resolvePathChoice(std::optional<PathChoice> stored, bool isFastLaneModel,
                                    std::optional<DirectClaimFailure> priorFailure) {
    if (stored.has_value()) { return *stored; }
    if (isFastLaneModel && !priorFailure.has_value()) { return PathChoice::Direct; }
    return PathChoice::Standard;
}

} // namespace dish::reducer
