// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ApplyBindingMachine — the apply sequencer behind the wizard's Review page and
// Configure binding's Apply. Both surfaces show the SAME overlay: one step per
// real async action, because the Connection step alone can sit for 20 s while
// Windows hands the device over, and a spinner cannot say that.
//
//   Connection   the USB path switch (raw-HID claim / release). SKIPPED when the
//                draft's path already matches the slot's.
//   Destination  the REST round-trip that writes the binding.
//
// Two rules carry most of the meaning:
//   * A Direct claim that times out is a FALLBACK, not a failure. The pad still
//     streams over Standard, so the run continues to Binding and the caller
//     raises a WARNING toast (`directFellBack`), never an error.
//   * Cancel is accepted ONLY while the Connection step is active. Aborting a
//     claim drops back to Standard, which is safe; the 8 s REST round-trip is
//     short enough not to need an escape and cannot be half-applied.
//
// Pure, Qt-free, header-only: every phase x event is unit-testable with no
// timers, no sockets and no satellite. The wall clock lives in AppViewModel,
// which feeds it as Tick / *TimedOut events.

#pragma once

#include <optional>
#include <type_traits>
#include <variant>

namespace dish::reducer {

enum class ApplyStep { Connection, Destination };
enum class ApplyStepState { Pending, Active, Done, Failed, Skipped };
enum class ApplyPhase { Idle, SwitchingPath, Binding, Succeeded, Failed, Cancelled };
enum class ApplyFailure { SlotGone, PathClaimTimeout, HostUnreachable, BindRejected, Cancelled };

struct ApplyState {
    ApplyPhase phase = ApplyPhase::Idle;
    ApplyStepState connection = ApplyStepState::Pending;
    ApplyStepState destination = ApplyStepState::Pending;
    bool directFellBack = false; // -> WARNING toast, not error
    std::optional<ApplyFailure> failure;
    int elapsedMsOnStep = 0; // drives the 4 s slow hint
    // Carried from Start so a settled path can tell "the claim we asked for did
    // not land" (a fallback) from "we asked to go back to Standard and did".
    bool wantsDirect = false;
};

namespace apply_event {
struct Start {
    bool needsPathSwitch = false;
    bool wantsDirect = false;
};
struct PathSettled {
    bool direct = false;
}; // pathPhase left `claiming`
struct PathTimedOut {}; // 20 s
struct BindAccepted {};
struct BindRejected {
    bool unreachable = false;
};
struct BindTimedOut {}; // 8 s
struct SlotVanished {};
struct Cancel {};
struct Tick {
    int deltaMs = 0;
};
} // namespace apply_event

using ApplyEvent =
    std::variant<apply_event::Start, apply_event::PathSettled, apply_event::PathTimedOut,
                 apply_event::BindAccepted, apply_event::BindRejected, apply_event::BindTimedOut,
                 apply_event::SlotVanished, apply_event::Cancel, apply_event::Tick>;

// True while the run is still moving (and therefore still cancel-/tick-able).
inline bool applyInFlight(const ApplyState& s) {
    return s.phase == ApplyPhase::SwitchingPath || s.phase == ApplyPhase::Binding;
}

// True iff the escape hatch is offered: the Connection step, and only it.
inline bool applyCancellable(const ApplyState& s) { return s.phase == ApplyPhase::SwitchingPath; }

inline ApplyState reduceApply(const ApplyState& s, const ApplyEvent& e) {
    ApplyState next = s;

    // Move onto the Destination step. Shared by the three ways the Connection
    // step can end: settled, timed out, or skipped outright.
    const auto enterBinding = [](ApplyState& st) {
        st.phase = ApplyPhase::Binding;
        st.destination = ApplyStepState::Active;
        st.elapsedMsOnStep = 0;
    };

    return std::visit(
        [&](auto&& ev) -> ApplyState {
            using E = std::decay_t<decltype(ev)>;

            if constexpr (std::is_same_v<E, apply_event::Start>) {
                // A Start always restarts the run — a retry after a failure is
                // the same call, and the draft is still intact.
                next = ApplyState{};
                next.wantsDirect = ev.wantsDirect;
                if (ev.needsPathSwitch) {
                    next.phase = ApplyPhase::SwitchingPath;
                    next.connection = ApplyStepState::Active;
                } else {
                    next.connection = ApplyStepState::Skipped;
                    enterBinding(next);
                }
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::PathSettled>) {
                if (next.phase != ApplyPhase::SwitchingPath) { return next; }
                next.connection = ApplyStepState::Done;
                // Asked for Direct and got Standard: the claim lost. Warn, don't
                // fail — the pad streams either way.
                next.directFellBack = next.wantsDirect && !ev.direct;
                enterBinding(next);
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::PathTimedOut>) {
                if (next.phase != ApplyPhase::SwitchingPath) { return next; }
                next.connection = ApplyStepState::Done;
                next.directFellBack = true;
                enterBinding(next);
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::BindAccepted>) {
                if (next.phase != ApplyPhase::Binding) { return next; }
                next.destination = ApplyStepState::Done;
                next.phase = ApplyPhase::Succeeded;
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::BindRejected>) {
                if (next.phase != ApplyPhase::Binding) { return next; }
                next.destination = ApplyStepState::Failed;
                next.phase = ApplyPhase::Failed;
                next.failure =
                    ev.unreachable ? ApplyFailure::HostUnreachable : ApplyFailure::BindRejected;
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::BindTimedOut>) {
                if (next.phase != ApplyPhase::Binding) { return next; }
                next.destination = ApplyStepState::Failed;
                next.phase = ApplyPhase::Failed;
                // A REST round-trip that never answers IS an unreachable host.
                next.failure = ApplyFailure::HostUnreachable;
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::SlotVanished>) {
                // The pad went away mid-apply (a Direct claim retires the
                // framework slot id). Terminal from any live phase; a run that
                // already finished keeps its result.
                if (next.phase == ApplyPhase::Succeeded || next.phase == ApplyPhase::Failed ||
                    next.phase == ApplyPhase::Cancelled) {
                    return next;
                }
                if (next.connection == ApplyStepState::Active) {
                    next.connection = ApplyStepState::Failed;
                }
                if (next.destination == ApplyStepState::Active) {
                    next.destination = ApplyStepState::Failed;
                }
                next.phase = ApplyPhase::Failed;
                next.failure = ApplyFailure::SlotGone;
                return next;
            } else if constexpr (std::is_same_v<E, apply_event::Cancel>) {
                // Accepted ONLY while the claim is in flight.
                if (next.phase != ApplyPhase::SwitchingPath) { return next; }
                next.connection = ApplyStepState::Failed;
                next.phase = ApplyPhase::Cancelled;
                next.failure = ApplyFailure::Cancelled;
                return next;
            } else {
                static_assert(std::is_same_v<E, apply_event::Tick>);
                if (!applyInFlight(next)) { return next; }
                next.elapsedMsOnStep += ev.deltaMs;
                return next;
            }
        },
        e);
}

} // namespace dish::reducer
