// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PairingMachine — the explicit FORWARD (user-types-a-PIN, Path A) pairing
// lifecycle FSM. Pure, Qt-free, and exhaustively testable, mirroring the shape
// of UsbPathMachine.h: a total reducer `(state, event) -> state`, events as a
// std::variant, every (phase x event) pair defined and never throwing.
//
// The gap this closes: forward pairing today (WifiConnectionManager::pairWithPin
// / pairAndConnect) has NO observable lifecycle. In-flight is a polled QSet,
// "success" is inferred from an online-count rising edge, and every distinct
// failure reason — wrong PIN, protocol skew, unreachable, declined — collapses
// into ONE transient errorMessage() toast. Reverse pairing already has a proper
// phase enum (see ReversePairing.h); this gives forward pairing the same
// first-class, retained, typed lifecycle.
//
// Decision boundary (what is and isn't here):
//   * This reducer CONSUMES the existing pure pairing classifier
//     `classifyPair` from RestOutcome.h — it does NOT invent a new one. The
//     manager feeds each POST /api/pair reply's already-classified `PairVerdict`
//     in as a ReplyClassified event; the table below is the single place that
//     decides what each verdict MEANS for the forward lifecycle. Today that
//     decision is duplicated across three std::visit blocks in
//     WifiConnectionManager.cpp; this unifies it.
//   * Success is NOT inferred from a key adoption alone. A classified `Success`
//     means "the key was adopted and the session is opening" — it keeps us in
//     Submitting. Only an explicit SessionConfirmedLive event (the session
//     actually reached Connected) drives Succeeded. This deliberately kills the
//     online-count rising-edge heuristic: the lifecycle reports success exactly
//     when the session is live, not a beat early.
//   * No IO and no effects: unlike UsbPathMachine the forward-pair coordinator's
//     side effects (adopt key, openSession, emit toast) are simple and already
//     live in the manager, so this reducer returns only the next STATE. The
//     manager reads the phase/failure transition and acts. Keeping it
//     state-only matches ReversePairing's "decide, don't act" split.
//
// Qt-free by construction: std::string / std::optional / std::variant only.

#pragma once

#include "core/reducer/RestOutcome.h"

#include <optional>
#include <string>
#include <utility>
#include <variant>

namespace dish::reducer {

// ── Phase ─────────────────────────────────────────────────────────────────────
// The forward-pairing lifecycle. Linear on the happy path
// (Idle -> Submitting -> Succeeded); Failed is a terminal-but-retryable rest
// stop that retains WHY. Cancel returns to Idle from anywhere.
enum class PairPhase {
    Idle,       // nothing in flight; the resting state.
    Submitting, // a PIN was submitted and we are awaiting the reply / the
                // session going live. A classified Success stays HERE (key
                // adopted, session opening) until SessionConfirmedLive.
    Succeeded,  // the session actually reached Connected. Terminal-success.
    Failed,     // the submit failed; `failure` says why. Retryable via Submit.
};

// ── Failure reason ──────────────────────────────────────────────────────────
// The typed, RETAINED reason a forward submit failed — the value the old single
// errorMessage() toast threw away. Mapped 1:1 from the failing PairVerdict arms
// in reducePairing (see the mapping notes there).
enum class PairFailure {
    WrongPin,        // reachable, parsed, but no usable key adopted: the PIN was
                     // wrong / not accepted. Maps from PairVerdict::AuthRequired
                     // ("reachable but no key" — the contract's name for it).
    VersionMismatch, // 409: the app and satellite speak different protocol
                     // versions. Maps from PairVerdict::VersionMismatch.
    Unreachable,     // transport failure / empty body: the satellite never
                     // answered. Maps from PairVerdict::Unreachable.
    Declined,        // the satellite refused the pair (operator denied / device
                     // rejected). Has no PairVerdict arm of its own on the
                     // forward POST path — it is surfaced by the manager via a
                     // dedicated Declined classification it already owns; the
                     // enum carries it so the forward and reverse vocabularies
                     // match and a future wiring can map a declined reply to it.
};

// ── State ─────────────────────────────────────────────────────────────────────
// One forward-pairing attempt's state. INVARIANTS the reducer upholds:
//   * `failure` is populated IFF phase == Failed (cleared on every other phase).
//   * `pin` is the PIN carried while Submitting; it is cleared on Idle and on
//     Succeeded, and retained on Failed so a retry/UI can show what was tried.
struct PairingState {
    PairPhase phase = PairPhase::Idle;
    std::optional<PairFailure> failure; // set iff phase == Failed
    std::string pin;                    // carried while Submitting / Failed

    bool operator==(const PairingState& o) const {
        return phase == o.phase && failure == o.failure && pin == o.pin;
    }
    bool operator!=(const PairingState& o) const { return !(*this == o); }
};

// ── Events ────────────────────────────────────────────────────────────────────

namespace pair_event {

// The user submitted a PIN. Starts (or restarts) a pairing attempt.
struct Submit {
    std::string pin;
    bool operator==(const Submit& o) const { return pin == o.pin; }
    bool operator!=(const Submit& o) const { return !(*this == o); }
};

// The network reply to the POST /api/pair, ALREADY classified by the existing
// pure classifier (classifyPair in RestOutcome.h). The reducer maps the verdict
// to the next phase — it does not re-classify.
struct ReplyClassified {
    PairVerdict verdict = PairVerdict::Unreachable;
    bool operator==(const ReplyClassified& o) const { return verdict == o.verdict; }
    bool operator!=(const ReplyClassified& o) const { return !(*this == o); }
};

// The session actually reached Connected (live). The ONLY event that drives
// Succeeded — replaces the old online-count rising-edge inference.
struct SessionConfirmedLive {
    bool operator==(const SessionConfirmedLive&) const { return true; }
    bool operator!=(const SessionConfirmedLive&) const { return false; }
};

// The user (or a superseding request) cancelled. Returns to Idle from any phase.
struct Cancel {
    bool operator==(const Cancel&) const { return true; }
    bool operator!=(const Cancel&) const { return false; }
};

} // namespace pair_event

using PairEvent = std::variant<pair_event::Submit, pair_event::ReplyClassified,
                               pair_event::SessionConfirmedLive, pair_event::Cancel>;

// ── Reducer ─────────────────────────────────────────────────────────────────
// The total forward-pairing reducer: (state, event) -> next state. Defined for
// EVERY (phase x event); never throws. The rules, by event:
//
//   Submit{pin}
//     Idle / Failed / Succeeded -> Submitting(pin), failure cleared.
//       From Failed this is a RETRY: the prior failure is dropped so the UI
//       leaves the error state. From Succeeded it is a fresh attempt (e.g. the
//       session dropped and the user re-pairs). The submitted pin is carried.
//     Submitting -> Submitting(pin): a re-submit while one is already in flight
//       just adopts the newest pin (the manager single-flights the wire).
//
//   ReplyClassified{verdict}  — only meaningful while Submitting:
//     Success         -> stay Submitting (key adopted, session opening; we wait
//                        for SessionConfirmedLive before reporting Succeeded).
//     Pending         -> stay Submitting. On a FORWARD submit a Pending is NOT a
//                        terminal failure: Path A doesn't expect it, but the
//                        manager may still resolve it (or it degrades), so we
//                        keep waiting rather than flipping to Failed.
//     AuthRequired    -> Failed(WrongPin).
//     VersionMismatch -> Failed(VersionMismatch).
//     Unreachable     -> Failed(Unreachable).
//     A ReplyClassified that arrives in any non-Submitting phase is a late /
//     stale reply for a settled attempt and is IGNORED (state unchanged).
//
//   SessionConfirmedLive
//     Submitting -> Succeeded (pin + failure cleared). This is the ONLY path to
//       Succeeded. In any other phase it is a stray confirmation (no attempt is
//       waiting on it) and is IGNORED.
//
//   Cancel
//     Any phase -> Idle (a fresh, empty state). Total and unconditional.
//
// Anything not named above is a no-op for that phase (returns the state
// unchanged), making every combination explicit.
inline PairingState reducePairing(const PairingState& s, const PairEvent& event) {
    return std::visit(
        [&](const auto& e) -> PairingState {
            using E = std::decay_t<decltype(e)>;

            // ── Submit: start or restart an attempt from any phase ──────────
            if constexpr (std::is_same_v<E, pair_event::Submit>) {
                PairingState next;
                next.phase = PairPhase::Submitting;
                next.failure = std::nullopt; // a new attempt clears any prior reason
                next.pin = e.pin;            // carry the PIN while submitting
                return next;
            }

            // ── ReplyClassified: map the verdict, only while Submitting ─────
            else if constexpr (std::is_same_v<E, pair_event::ReplyClassified>) {
                if (s.phase != PairPhase::Submitting) {
                    return s; // late/stale reply for a settled attempt — ignore
                }
                switch (e.verdict) {
                case PairVerdict::Success:
                    // Key adopted, session opening — NOT done yet. Stay put and
                    // wait for SessionConfirmedLive to confirm the live session.
                    return s;
                case PairVerdict::Pending:
                    // Forward Path A doesn't expect Pending; it is not a terminal
                    // failure here. Keep Submitting (the manager resolves it).
                    return s;
                case PairVerdict::AuthRequired: {
                    // Reachable but no usable key adopted: the PIN was rejected.
                    PairingState next = s;
                    next.phase = PairPhase::Failed;
                    next.failure = PairFailure::WrongPin;
                    return next; // pin retained for the UI / a retry
                }
                case PairVerdict::VersionMismatch: {
                    PairingState next = s;
                    next.phase = PairPhase::Failed;
                    next.failure = PairFailure::VersionMismatch;
                    return next;
                }
                case PairVerdict::Unreachable: {
                    PairingState next = s;
                    next.phase = PairPhase::Failed;
                    next.failure = PairFailure::Unreachable;
                    return next;
                }
                }
                // Defensive: a bogus verdict cast lands here. The switch is
                // exhaustive over the enum, so this is unreachable in practice;
                // treat an unknown verdict as "keep waiting" rather than throw.
                return s;
            }

            // ── SessionConfirmedLive: the ONLY path to Succeeded ────────────
            else if constexpr (std::is_same_v<E, pair_event::SessionConfirmedLive>) {
                if (s.phase != PairPhase::Submitting) {
                    return s; // no attempt is awaiting confirmation — ignore
                }
                PairingState next;
                next.phase = PairPhase::Succeeded;
                next.failure = std::nullopt;
                next.pin.clear(); // attempt is done; don't retain the PIN
                return next;
            }

            // ── Cancel: unconditional return to Idle from any phase ─────────
            else if constexpr (std::is_same_v<E, pair_event::Cancel>) {
                return PairingState{}; // fresh Idle state (no failure, no pin)
            }

            // ── Total fallback (no event type reaches here) ─────────────────
            else {
                return s;
            }
        },
        event);
}

} // namespace dish::reducer
