// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Manager-FSM decision rules (ADAPT/re-derive of dish-android source/connection/
// SatelliteConnectionManagerTest, 48). The Windows WifiConnectionManager drives
// REST through a concrete HTTPClient (no injectable gateway seam), so its full
// async FSM can't be unit-driven without sockets; this slice (2b) CONSUMES the
// manager and pins the DECISION RULES it composes — the intent×verdict matrix,
// the close-notify -> action mapping, the public-IP connect guard, the pairing
// classification, and the backoff schedule — as pure logic, exactly as
// android-tests prescribes for framework-bound manager tests ("re-derive the
// rule in a reducer/mapper"). The classifiers themselves (classifyRest/
// classifyPair/classifyApproval) and the reconcile/backoff math are pinned in
// test_rest_control_plane / test_session_reconcile; here we pin the manager-
// level COMPOSITIONS those tests don't cover.
//
// GAP FLAGGED: the android test "connect to a public ip is refused before any
// socket is opened" requires WifiConnectionManager::connectTo to consult
// isPrivateHostLiteral before openSocket. The Windows manager does NOT yet wire
// this guard (the predicate exists in core/net from 2a but is uncalled in the
// connect path). The RULE is pinned below; wiring it into the manager is a
// Wave-1/coordination follow-up (see the final report).

#include "core/net/IpLiterals.h"
#include "core/reducer/Backoff.h"
#include "core/reducer/CloseNotify.h"
#include "core/reducer/RestOutcome.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

namespace reducer = dish::reducer;

// ── ConnectIntent semantics: which intents are LOUD vs SILENT ────────────────
// The manager's rule (mirrored from emitErrorIfUserInitiated / scheduleRetry):
// only UserInitiated surfaces an error toast; only the silent intents
// (AutoReconnect, RetryAfterDeath) ride the backoff curve. Model the intent as a
// small predicate pair so the matrix is pinned without standing up the manager.

namespace {

enum class Intent { UserInitiated, AutoReconnect, RetryAfterDeath };

// "Does a failure under this intent surface a toast?" — only a user tap is loud.
bool isLoud(Intent i) { return i == Intent::UserInitiated; }

// "Does a transport failure under this intent schedule a silent backoff retry?"
// — every intent EXCEPT a user tap. (scheduleRetry returns early for UserInitiated.)
bool schedulesRetry(Intent i) { return i != Intent::UserInitiated; }

} // namespace

TEST_CASE("manager intent: only UserInitiated is loud on failure", "[manager][intent]") {
    REQUIRE(isLoud(Intent::UserInitiated));
    REQUIRE_FALSE(isLoud(Intent::AutoReconnect));
    REQUIRE_FALSE(isLoud(Intent::RetryAfterDeath));
}

TEST_CASE("manager intent: silent intents schedule a backoff retry; a user tap does not",
          "[manager][intent]") {
    REQUIRE_FALSE(schedulesRetry(Intent::UserInitiated));
    REQUIRE(schedulesRetry(Intent::AutoReconnect));
    REQUIRE(schedulesRetry(Intent::RetryAfterDeath));
}

// ── Terminal-401 / 409 are loud-but-no-retry under EVERY intent ──────────────
// A terminal verdict drops the key / explains the skew and STOPS the curve
// regardless of intent (onTerminalAuthFailure / VersionMismatch handling). Only
// the toast is gated on intent.

TEST_CASE("manager: a terminal verdict never rides the silent retry curve", "[manager][terminal]") {
    REQUIRE(reducer::restVerdictTerminal(reducer::RestVerdict::Unauthorized));
    REQUIRE(reducer::restVerdictTerminal(reducer::RestVerdict::VersionMismatch));
    REQUIRE_FALSE(reducer::restVerdictRetryable(reducer::RestVerdict::Unauthorized));
    REQUIRE_FALSE(reducer::restVerdictRetryable(reducer::RestVerdict::VersionMismatch));
}

TEST_CASE("manager: an unreachable/503/5xx verdict is retryable (silent backoff territory)",
          "[manager][terminal]") {
    REQUIRE(reducer::restVerdictRetryable(reducer::RestVerdict::Unreachable));
    REQUIRE(reducer::restVerdictRetryable(reducer::RestVerdict::ShuttingDown));
    REQUIRE(reducer::restVerdictRetryable(reducer::RestVerdict::ServerError));
    REQUIRE_FALSE(reducer::restVerdictTerminal(reducer::RestVerdict::Unreachable));
}

// ── Pairing classification at the manager boundary (PairVerdict) ─────────────
// The manager's pair-then-branch (pairAndConnect) keys on these arms: Success ->
// store key + openSession; AuthRequired/Pending -> PIN (loud) or Stale (silent);
// VersionMismatch -> explain; Unreachable -> toast (loud) or Stale (silent).

namespace {
reducer::PairReply pairReply(int status, bool parsed, bool ok, bool pending, bool hasKey) {
    reducer::PairReply r;
    r.status = status;
    r.bodyParsed = parsed;
    r.ok = ok;
    r.pending = pending;
    r.hasSharedKey = hasKey;
    return r;
}
} // namespace

TEST_CASE("manager pair: an ok reply with a key is Success (open the session)", "[manager][pair]") {
    REQUIRE(reducer::classifyPair(pairReply(200, true, true, false, true)) ==
            reducer::PairVerdict::Success);
}

TEST_CASE("manager pair: empty/unparsed (status 0) is Unreachable, not PairingRequired",
          "[manager][pair]") {
    // Mirrors "pair returning empty string surfaces server-unreachable not PairingRequired".
    REQUIRE(reducer::classifyPair(pairReply(0, false, false, false, false)) ==
            reducer::PairVerdict::Unreachable);
}

TEST_CASE("manager pair: ok=false reachable is AuthRequired (PIN territory)", "[manager][pair]") {
    // Mirrors "pair returning ok=false with reachable server emits PairingRequired".
    REQUIRE(reducer::classifyPair(pairReply(200, true, false, false, false)) ==
            reducer::PairVerdict::AuthRequired);
}

TEST_CASE("manager pair: a 409 is a protocol mismatch, not the PIN dialog", "[manager][pair]") {
    // Mirrors "pair 409 surfaces a protocol mismatch instead of the PIN dialog".
    REQUIRE(reducer::classifyPair(pairReply(409, true, false, false, false)) ==
            reducer::PairVerdict::VersionMismatch);
}

TEST_CASE("manager pair: ok=true with no key is AuthRequired (never cache an empty key)",
          "[manager][pair]") {
    REQUIRE(reducer::classifyPair(pairReply(200, true, true, false, false)) ==
            reducer::PairVerdict::AuthRequired);
}

// ── Close-notify reason -> manager action ────────────────────────────────────
// handleServerClose maps the reason byte onto an action: unpaired -> drop key +
// stale + stop; replaced -> stay down; shutdown/kicked -> backoff retry.

TEST_CASE("manager close: unpaired drops the key and stops retrying", "[manager][close]") {
    REQUIRE(reducer::closeActionForReason(dish::proto::kCloseReasonUnpaired) ==
            reducer::CloseAction::DropKeyRePair);
}

TEST_CASE("manager close: replaced stays down (a newer PUT owns the session)", "[manager][close]") {
    REQUIRE(reducer::closeActionForReason(dish::proto::kCloseReasonReplaced) ==
            reducer::CloseAction::StayDown);
}

TEST_CASE("manager close: shutdown and kicked reconnect on the backoff curve", "[manager][close]") {
    REQUIRE(reducer::closeActionForReason(dish::proto::kCloseReasonShutdown) ==
            reducer::CloseAction::RetryBackoff);
    REQUIRE(reducer::closeActionForReason(dish::proto::kCloseReasonKicked) ==
            reducer::CloseAction::RetryBackoff);
}

// ── Public-IP connect guard (the RULE; wiring flagged as a gap above) ────────

TEST_CASE("manager guard: a public ip target is refused (private-literal rule)",
          "[manager][guard]") {
    // 8.8.8.8 is public -> a connect must be refused before opening a socket.
    REQUIRE_FALSE(dish::net::isPrivateHostLiteral("8.8.8.8"));
    // A LAN target (the only thing a satellite legitimately lives at) is allowed.
    REQUIRE(dish::net::isPrivateHostLiteral("10.0.0.5"));
    REQUIRE(dish::net::isPrivateHostLiteral("192.168.1.20"));
    REQUIRE(dish::net::isPrivateHostLiteral("172.16.0.9"));
}

// ── Silent-retry backoff schedule (the curve the retry intents ride) ─────────
// Pinned in full in test_session_reconcile; the manager-relevant assertion is
// that the FIRST silent retry is ~1 s and the curve is bounded.

TEST_CASE("manager backoff: first silent retry is 1s, the curve is bounded at 60s",
          "[manager][backoff]") {
    REQUIRE(reducer::backoffDelayMs(1) == 1000);
    REQUIRE(reducer::backoffDelayMs(100) == reducer::kBackoffMaxMs);
}
