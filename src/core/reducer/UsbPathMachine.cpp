// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// reduce() implementation for the USB path FSM. Ported 1:1 from dish-android
// source/usb/UsbPathMachine.kt — the per-phase reducers, the unplug short-
// circuit, and the exact effect ordering each transition emits (the
// UsbPathMachineTest / UsbPathMachineEdgeCasesTest assertions pin the lists).

#include "core/reducer/UsbPathMachine.h"

namespace dish::reducer {

namespace {

// stay(c): no transition, no effects (Kotlin's `stay`).
Reduction stay(UsbController c) { return Reduction{std::move(c), {}}; }

// A fresh Direct attempt: clear any stale failure, hold the framework, claim.
Reduction startClaim(UsbController c) {
    c.phase = UsbPhase::Claiming;
    c.failure.reset();
    return Reduction{std::move(c), {effect::ClearFailure{}, effect::BeginHold{}, effect::Claim{}}};
}

// Helpers mirroring Kotlin's `event is X` dispatch via std::holds_alternative.
template <class T> const T* as(const UsbEvent& e) { return std::get_if<T>(&e); }

Reduction reduceRouted(const UsbController& c, const UsbEvent& event) {
    if (const auto* up = as<event::FrameworkUp>(event)) {
        UsbController n = c;
        n.frameworkId = up->id;
        return stay(std::move(n));
    }
    if (as<event::FrameworkDown>(event) != nullptr) {
        // Framework dropped while routed (cable jiggle or claim aftermath); wait
        // for it to return.
        UsbController n = c;
        n.phase = UsbPhase::AwaitingFramework;
        n.frameworkId.reset();
        n.userInitiated = false;
        return Reduction{std::move(n), {effect::StartTimeout{}}};
    }
    if (as<event::PermissionGranted>(event) != nullptr) {
        UsbController granted = c;
        granted.hasPermission = true;
        if (granted.desired == PathChoice::Direct) { return startClaim(std::move(granted)); }
        return stay(std::move(granted));
    }
    if (as<event::PermissionDenied>(event) != nullptr) {
        if (c.desired == PathChoice::Direct) {
            UsbController n = c;
            n.desired = PathChoice::Standard;
            n.failure = DirectClaimFailure::PermissionDenied;
            std::vector<UsbEffect> fx;
            fx.push_back(effect::SetPref{PathChoice::Standard});
            fx.push_back(effect::MarkFailure{DirectClaimFailure::PermissionDenied});
            if (c.userInitiated) { fx.push_back(effect::Notify{UsbNotice::SwitchToDirectFailed}); }
            return Reduction{std::move(n), std::move(fx)};
        }
        return stay(c);
    }
    if (const auto* ch = as<event::Choose>(event)) {
        if (ch->choice == PathChoice::Standard) {
            UsbController n = c;
            n.desired = PathChoice::Standard;
            return stay(std::move(n));
        }
        // Direct.
        UsbController wanting = c;
        wanting.desired = PathChoice::Direct;
        wanting.userInitiated = ch->userInitiated;
        if (wanting.hasPermission) { return startClaim(std::move(wanting)); }
        if (ch->userInitiated) {
            return Reduction{std::move(wanting), {effect::RequestPermission{}}};
        }
        return stay(std::move(wanting));
    }
    return stay(c);
}

Reduction reduceClaiming(const UsbController& c, const UsbEvent& event) {
    if (const auto* ok = as<event::ClaimSucceeded>(event)) {
        UsbController n = c;
        n.phase = UsbPhase::Direct;
        n.syntheticId = ok->syntheticId;
        n.frameworkId.reset();
        n.failure.reset();
        return Reduction{std::move(n), {effect::EndHold{}, effect::ClearFailure{}}};
    }
    if (const auto* failed = as<event::ClaimFailed>(event)) {
        if (failed->frameworkStolen) {
            // Kernel HID driver was detached by the claim; wait for the framework
            // device to come back so we can settle on Standard (and surface the
            // reason once it does).
            UsbController n = c;
            n.phase = UsbPhase::AwaitingFramework;
            n.syntheticId.reset();
            n.failure = failed->reason;
            return Reduction{std::move(n), {effect::StartTimeout{}}};
        }
        // Open/claim was rejected without ever stealing the interface, so the
        // framework slot is still live; drop straight back to Standard and say
        // why Direct didn't happen. Persist Standard too, or the failed pick is
        // silently re-attempted on every reconnect.
        UsbController n = c;
        n.phase = UsbPhase::Routed;
        n.desired = PathChoice::Standard;
        n.syntheticId.reset();
        n.failure = failed->reason;
        std::vector<UsbEffect> fx;
        fx.push_back(effect::EndHold{});
        fx.push_back(effect::SetPref{PathChoice::Standard});
        fx.push_back(effect::MarkFailure{failed->reason});
        if (c.userInitiated) { fx.push_back(effect::Notify{UsbNotice::SwitchToDirectFailed}); }
        return Reduction{std::move(n), std::move(fx)};
    }
    if (const auto* up = as<event::FrameworkUp>(event)) {
        UsbController n = c;
        n.frameworkId = up->id;
        return stay(std::move(n));
    }
    if (as<event::PermissionGranted>(event) != nullptr) {
        UsbController n = c;
        n.hasPermission = true;
        return stay(std::move(n));
    }
    return stay(c);
}

Reduction reduceDirect(const UsbController& c, const UsbEvent& event) {
    if (const auto* ch = as<event::Choose>(event)) {
        if (ch->choice == PathChoice::Standard) {
            // Release the interface but keep the synthetic as a held placeholder
            // while the framework device comes back; if it doesn't we stop in
            // RestoreStuck and let the user choose.
            UsbController n = c;
            n.phase = UsbPhase::AwaitingFramework;
            n.userInitiated = ch->userInitiated;
            n.failure.reset();
            return Reduction{std::move(n), {effect::Release{}, effect::StartTimeout{}}};
        }
        UsbController n = c;
        n.desired = PathChoice::Direct;
        return stay(std::move(n));
    }
    if (const auto* up = as<event::FrameworkUp>(event)) {
        UsbController n = c;
        n.frameworkId = up->id;
        return stay(std::move(n));
    }
    return stay(c);
}

Reduction reduceAwaiting(const UsbController& c, const UsbEvent& event) {
    if (const auto* up = as<event::FrameworkUp>(event)) {
        // Framework is back: settle on Standard, hand the binding over, drop the
        // placeholder.
        std::vector<UsbEffect> fx;
        if (c.syntheticId.has_value()) {
            fx.push_back(effect::RemoveSynthetic{*c.syntheticId});
        } else {
            fx.push_back(effect::EndHold{});
        }
        fx.push_back(effect::BindFramework{up->id});
        fx.push_back(effect::SetPref{PathChoice::Standard});
        if (c.failure.has_value()) {
            // Came from a failed Direct claim: show why on the re-enumerated card.
            fx.push_back(effect::MarkFailure{*c.failure});
            if (c.userInitiated) { fx.push_back(effect::Notify{UsbNotice::SwitchToDirectFailed}); }
        } else {
            fx.push_back(effect::ClearFailure{});
        }
        UsbController n = c;
        n.phase = UsbPhase::Routed;
        n.frameworkId = up->id;
        n.syntheticId.reset();
        n.desired = PathChoice::Standard;
        n.failure.reset();
        return Reduction{std::move(n), std::move(fx)};
    }
    if (as<event::Timeout>(event) != nullptr) {
        if (c.syntheticId.has_value()) {
            // Return-to-Standard never re-enumerated. Don't silently re-claim
            // Direct under the user; surface the stuck state with a live toggle.
            UsbController n = c;
            n.phase = UsbPhase::RestoreStuck;
            return Reduction{
                std::move(n),
                {effect::MarkRestoreStuck{}, effect::Notify{UsbNotice::RestoreFailed}}};
        }
        // Failed claim never recovered; the device is gone from the OS. Keep the
        // held placeholder visible as a "needs replug" card, and mark the reason
        // Dropped so the card asks for a physical replug instead of echoing the
        // stale claim error.
        UsbController n = c;
        n.phase = UsbPhase::NeedsReplug;
        n.desired = PathChoice::Standard;
        n.failure = DirectClaimFailure::Dropped;
        return Reduction{
            std::move(n),
            {effect::MarkNeedsReplug{}, effect::MarkFailure{DirectClaimFailure::Dropped},
             effect::SetPref{PathChoice::Standard}, effect::Notify{UsbNotice::NeedsReplug}}};
    }
    if (as<event::PermissionGranted>(event) != nullptr) {
        UsbController n = c;
        n.hasPermission = true;
        return stay(std::move(n));
    }
    return stay(c);
}

Reduction reduceRestoreStuck(const UsbController& c, const UsbEvent& event) {
    if (const auto* ch = as<event::Choose>(event)) {
        if (ch->choice == PathChoice::Direct) {
            // Go back to the Direct we know works.
            UsbController n = c;
            n.desired = PathChoice::Direct;
            n.userInitiated = ch->userInitiated;
            return Reduction{std::move(n), {effect::Reclaim{}}};
        }
        // Standard: try waiting for the framework once more (rarely succeeds
        // without a replug, but it's the user's call now).
        UsbController n = c;
        n.phase = UsbPhase::AwaitingFramework;
        n.userInitiated = ch->userInitiated;
        return Reduction{std::move(n), {effect::ClearRestoreStuck{}, effect::StartTimeout{}}};
    }
    if (const auto* ok = as<event::ClaimSucceeded>(event)) {
        // Reclaim succeeded: back on Direct.
        UsbController n = c;
        n.phase = UsbPhase::Direct;
        n.syntheticId = ok->syntheticId;
        n.desired = PathChoice::Direct;
        n.failure.reset();
        return Reduction{std::move(n),
                         {effect::SetPref{PathChoice::Direct}, effect::ClearFailure{},
                          effect::Notify{UsbNotice::RolledBackToDirect}}};
    }
    if (as<event::ClaimFailed>(event) != nullptr) {
        // Reclaim failed too: the device is gone. The synthetic placeholder is
        // dropped by the Reclaim effector; surface a Dropped reason so the
        // NeedsReplug card asks for a physical replug.
        UsbController n = c;
        n.phase = UsbPhase::NeedsReplug;
        n.syntheticId.reset();
        n.failure = DirectClaimFailure::Dropped;
        return Reduction{std::move(n),
                         {effect::MarkFailure{DirectClaimFailure::Dropped},
                          effect::Notify{UsbNotice::RestoreFailed}}};
    }
    if (const auto* up = as<event::FrameworkUp>(event)) {
        // The framework finally came back on its own: settle on Standard.
        std::vector<UsbEffect> fx;
        if (c.syntheticId.has_value()) {
            fx.push_back(effect::RemoveSynthetic{*c.syntheticId});
        } else {
            fx.push_back(effect::EndHold{});
        }
        fx.push_back(effect::BindFramework{up->id});
        fx.push_back(effect::SetPref{PathChoice::Standard});
        fx.push_back(effect::ClearRestoreStuck{});
        fx.push_back(effect::ClearFailure{});
        UsbController n = c;
        n.phase = UsbPhase::Routed;
        n.frameworkId = up->id;
        n.syntheticId.reset();
        n.desired = PathChoice::Standard;
        n.failure.reset();
        return Reduction{std::move(n), std::move(fx)};
    }
    if (as<event::PermissionGranted>(event) != nullptr) {
        UsbController n = c;
        n.hasPermission = true;
        return stay(std::move(n));
    }
    return stay(c);
}

Reduction reduceNeedsReplug(const UsbController& c, const UsbEvent& event) {
    if (const auto* up = as<event::FrameworkUp>(event)) {
        // The OS finally gave the device back: return to Standard.
        UsbController n = c;
        n.phase = UsbPhase::Routed;
        n.frameworkId = up->id;
        n.failure.reset();
        return Reduction{std::move(n), {effect::BindFramework{up->id}, effect::ClearFailure{}}};
    }
    if (const auto* ch = as<event::Choose>(event)) {
        UsbController n = c;
        n.desired = ch->choice;
        n.userInitiated = ch->userInitiated;
        return stay(std::move(n));
    }
    if (as<event::PermissionGranted>(event) != nullptr) {
        UsbController n = c;
        n.hasPermission = true;
        return stay(std::move(n));
    }
    return stay(c);
}

} // namespace

Reduction reduce(const UsbController& c, const UsbEvent& event) {
    // Physical unplug wins from any phase: tear down and forget.
    if (std::holds_alternative<event::UsbUnplugged>(event)) {
        std::vector<UsbEffect> fx;
        if (c.syntheticId.has_value()) { fx.push_back(effect::RemoveSynthetic{*c.syntheticId}); }
        if (c.phase == UsbPhase::Claiming || c.phase == UsbPhase::AwaitingFramework ||
            c.phase == UsbPhase::RestoreStuck || c.phase == UsbPhase::NeedsReplug) {
            fx.push_back(effect::EndHold{});
        }
        return Reduction{std::nullopt, std::move(fx)};
    }
    switch (c.phase) {
    case UsbPhase::Routed:
        return reduceRouted(c, event);
    case UsbPhase::Claiming:
        return reduceClaiming(c, event);
    case UsbPhase::Direct:
        return reduceDirect(c, event);
    case UsbPhase::AwaitingFramework:
        return reduceAwaiting(c, event);
    case UsbPhase::RestoreStuck:
        return reduceRestoreStuck(c, event);
    case UsbPhase::NeedsReplug:
        return reduceNeedsReplug(c, event);
    }
    return stay(c); // unreachable; switch is total over UsbPhase
}

} // namespace dish::reducer
