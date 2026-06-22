// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure, Qt-network-free decision core for HOST-INITIATED (reverse) pairing —
// Path B. The dish shows a 4-digit clientPin, POSTs it to /api/pair (which the
// server stages as a pending grant), then polls GET /api/pair/status until the
// operator approves on the satellite. The poll loop's NEXT-ACTION decision is
// extracted here as a total, deterministic function so it unit-tests without
// sockets, Qt, or a clock (the live WifiConnectionManager methods open real
// Winsock work that the test process must join — there is no network seam, so
// driving them in a test hangs). The manager owns the timer + the live
// pairStatus call; it feeds each reply's classifyApproval verdict plus the
// elapsed-vs-deadline clock reading THROUGH this function and acts on the result.
//
// Mirrors the android Path-B poll loop's terminal/keep-waiting/timeout rules.

#pragma once

#include "core/reducer/RestOutcome.h"

#include <cstdint>

namespace dish::reducer {

// What the reverse-pairing poll loop should do next, decided purely from the
// latest approval verdict and the elapsed-vs-deadline clock reading.
//   Approve     — operator approved AND a usable key arrived: adopt + openSession.
//   Decline     — operator denied: abort with the "declined" error (TERMINAL).
//   KeepPolling  — still pending / momentarily unreachable, and time remains.
//   TimeOut      — no resolution by the deadline: abort with the timeout error.
enum class ReversePairingAction { Approve, Decline, KeepPolling, TimeOut };

// Decide the next poll action. TOTAL over every ApprovalVerdict × clock reading.
//
// A Declined verdict is terminal REGARDLESS of the clock — a denied grant never
// becomes approved, so we stop immediately rather than burning the deadline.
// Approved is likewise terminal (classifyApproval only returns Approved when a
// usable sharedKey is present — an "approved" with no key already degraded to
// Pending upstream, so we never Approve without a key here).
//
// Pending / Unreachable are the "still waiting" verdicts: keep polling until the
// deadline. The deadline is INCLUSIVE — at elapsed == deadlineMs we have spent
// the full budget, so that boundary is a TimeOut (one more poll would push us
// over). A non-positive deadline times out on the first non-terminal reply.
inline ReversePairingAction nextReversePairingAction(ApprovalVerdict approval,
                                                     std::int64_t elapsedMs,
                                                     std::int64_t deadlineMs) {
    switch (approval) {
    case ApprovalVerdict::Approved:
        return ReversePairingAction::Approve;
    case ApprovalVerdict::Declined:
        return ReversePairingAction::Decline;
    case ApprovalVerdict::Pending:
    case ApprovalVerdict::Unreachable:
        // Still waiting — keep polling only while we are strictly inside the
        // budget; the inclusive boundary (and anything past it) is a timeout.
        return (elapsedMs >= deadlineMs) ? ReversePairingAction::TimeOut
                                         : ReversePairingAction::KeepPolling;
    }
    // Unreachable-in-practice: the switch is exhaustive over the enum, but a
    // bogus cast lands here — treat it as "keep waiting if time remains".
    return (elapsedMs >= deadlineMs) ? ReversePairingAction::TimeOut
                                     : ReversePairingAction::KeepPolling;
}

// ── 4-digit PIN shape ────────────────────────────────────────────────────────
// The reverse-pairing PIN is exactly 4 decimal digits, zero-padded. The VALUE is
// random (generated at the manager from a seam-friendly source), but its SHAPE
// is fixed by the contract; both the formatter and the validator live here so a
// test pins the shape without depending on randomness.

inline constexpr int kReversePinDigits = 4;
inline constexpr int kReversePinModulus = 10000; // 10^kReversePinDigits

// Format a raw integer as a 4-digit, zero-padded PIN string. The input is
// reduced modulo 10000 so any source (a 32-bit random draw) maps onto the valid
// range — generation stays seam-friendly (feed it a fixed number in a test) and
// the output shape is always exactly 4 digits. Returns std::string so the helper
// is Qt-free and unit-testable in the reducer layer.
inline std::string formatReversePin(std::uint32_t raw) {
    const unsigned value = static_cast<unsigned>(raw % kReversePinModulus);
    std::string out(kReversePinDigits, '0');
    unsigned v = value;
    for (int i = kReversePinDigits - 1; i >= 0; --i) {
        out[static_cast<std::size_t>(i)] = static_cast<char>('0' + (v % 10));
        v /= 10;
    }
    return out;
}

// True iff `pin` is exactly 4 ASCII decimal digits — the shape the server's
// Path-B compare expects. Used to reject a malformed displayed PIN before it
// ever hits the wire (a defensive total predicate; the formatter above always
// produces a valid one).
inline bool isValidReversePin(const std::string& pin) {
    if (pin.size() != static_cast<std::size_t>(kReversePinDigits)) { return false; }
    for (const char c : pin) {
        if (c < '0' || c > '9') { return false; }
    }
    return true;
}

} // namespace dish::reducer
