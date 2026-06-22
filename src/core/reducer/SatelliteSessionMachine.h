// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteSessionMachine — the explicit per-satellite SESSION lifecycle FSM.
// Pure, Qt-free, exhaustively-tested. A total reducer
// `Reduction reduce(state, event)` where every (phase x event) pair is defined,
// never throws, and side effects are returned AS DATA (a std::vector<SessionEffect>);
// `reduce` performs no IO and reads no clock. Mirrors the shape of
// UsbPathMachine.h (effects-as-data Reduction) but is HEADER-ONLY with all logic
// inline, like ReversePairing.h / PairingMachine.h, so it needs no .cpp / CMake
// edit. The coordinator (a future SatelliteSessionCoordinator wrapping the
// dish::net::WifiConnectionManager callbacks) turns world changes into events,
// runs `reduce`, and executes the returned effects against the real subsystems
// (HTTPClient, SatelliteClient, ConnectionStore).
//
// The gap this closes (from the architecture audit). The satellite session
// lifecycle today is an imperative async state machine smeared across
// WifiConnectionManager callbacks (openSession / pairAndConnect / reconcile /
// scheduleRetry / handleServerClose / onTerminalAuthFailure), with the SAME
// decision block duplicated in THREE std::visit arms. Consequences this table
// fixes:
//   (1) Faltering / degraded is a DEFINED SessionState that is NEVER ENTERED
//       today — the native alive-poll only exposes a binary isAlive(), so the
//       2-miss "not responding" threshold (SatelliteClient::kHeartbeatMissNotResponding)
//       is computed nowhere and a stuttering link reads "Online" until it
//       hard-drops. Here HeartbeatMiss{count >= notResponding} enters Faltering.
//   (2) Reconnect / backoff is INVISIBLE: retryAttempts_ is a private QHash +
//       a QTimer::singleShot, with no observable "Reconnecting (n), next try in …"
//       state. Here Reconnecting is a first-class phase carrying retryAttempt +
//       nextRetryAtMs, and ScheduleRetry{delayMs} is a returned effect.
//   (3) Connection FAILURE is a fire-and-forget errorMessage() toast: the reason
//       (unreachable / version-mismatch / declined / 401-terminal) is never
//       captured on the connection. Here SessionFailure is a typed, RETAINED
//       reason carried on the model.
//
// Decision boundary (what is and isn't here):
//   * This reducer CONSUMES the existing pure classifiers — it does NOT
//     re-classify. The coordinator builds a RestReply / PairReply, runs
//     classifyRest / classifyPair (RestOutcome.h), and feeds the resulting
//     RestVerdict / PairVerdict in as a RestClassified / PairClassified event.
//     A server close-notify reason byte is mapped through closeActionForReason
//     (CloseNotify.h) at the call boundary and fed in as Closed{action}. The
//     retry delay is computed with backoffDelayMs (Backoff.h) and carried in the
//     ScheduleRetry effect; the table below is the single place that decides what
//     each verdict / action MEANS for the session lifecycle.
//   * CLOCK-FREE by construction. reduce() never reads wall time. A retry's
//     absolute deadline (nextRetryAtMs) is NOT stamped here — the reducer emits
//     ScheduleRetry{delayMs} (delay from backoffDelayMs) and the coordinator
//     stamps `now + delayMs` onto the model when it arms the timer (the same
//     "decide, don't act" split ReversePairing uses for the deadline). This keeps
//     the FSM deterministic, resumable, and unit-testable without a fake clock.
//     nextRetryAtMs is therefore carried THROUGH the model as 0 here and only set
//     by the coordinator; reduce() resets it to 0 whenever it leaves a retry.
//
// Compatibility with SatelliteLinkState.h (the UI maps a session through it).
// SatelliteLinkState defines SessionPresence { Idle, Linking, Live, Faltering,
// Stale } and satelliteLinkState(presence, isStale, isDiscovered) -> UiLinkState.
// This machine's richer SessionPhase collapses onto that presence axis as below,
// so the existing presence->LinkState mapper keeps working unchanged:
//
//     SessionPhase           SessionPresence    isStale flag the coordinator sets
//     ───────────            ───────────────    ─────────────────────────────────
//     Discovered          -> Idle               false
//     Pairing             -> Linking            false
//     Linking             -> Linking            false
//     Live                -> Live               false
//     Faltering           -> Faltering          false
//     Reconnecting        -> Linking            false  (silent retry reads
//                                                       "Connecting…", matching the
//                                                       manager's markStale->Linking
//                                                       chip cue today)
//     Stale               -> Stale              true   ("Needs pairing" / kept key)
//     Failed              -> Stale              true   (terminal cause retained in
//                                                       `failure`; chip "Needs pairing")
//
// The helper sessionPhaseToPresence() below makes that mapping a checkable pure
// function so the coordinator and UI agree by construction.
//
// Qt-free by construction: std::optional / std::variant / std::vector / integral
// types only.

#pragma once

#include "core/reducer/Backoff.h"
#include "core/reducer/CloseNotify.h"
#include "core/reducer/RestOutcome.h"
#include "core/reducer/SatelliteLinkState.h"

#include <optional>
#include <variant>
#include <vector>

namespace dish::reducer {

// ── Heartbeat thresholds ──────────────────────────────────────────────────────
// The alive-poll miss counts that drive the degraded / dead transitions. Read
// 1:1 from the real values on dish::net::SatelliteClient (SatelliteClient.h:
// kHeartbeatMissNotResponding = 2, kHeartbeatMissMax = 5; cadence 2000 ms,
// verified against the satellite's types.h HEARTBEAT_INTERVAL_SEC=2 /
// HEARTBEAT_MISS_MAX=5). Duplicated here as named constants so the PURE reducer
// carries no Network/Qt dependency; they MUST stay in lockstep with
// SatelliteClient's. `kHeartbeatMissNotResponding` is the count at which a live
// session degrades to Faltering (the bug today: never computed); `kHeartbeatMissDead`
// is the count at which the session is declared dead and we drop to Reconnecting.
inline constexpr int kHeartbeatMissNotResponding = 2; // >= this -> Faltering
inline constexpr int kHeartbeatMissDead = 5;          // >= this -> Reconnecting (dead)

// ── Phase ─────────────────────────────────────────────────────────────────────
// One satellite session's lifecycle. Maps onto SatelliteLinkState's
// SessionPresence (see the file header table + sessionPhaseToPresence below).
enum class SessionPhase {
    Discovered,   // resting: known/remembered but no live session. Presence: Idle.
    Pairing,      // a POST /api/pair handshake is in flight (no usable key yet).
                  // Presence: Linking. Only entered when Connect needs a pair first.
    Linking,      // session PUT / auth handshake in flight (key in hand). Presence:
                  // Linking.
    Live,         // UDP tuple bound, heartbeat acks flowing. Presence: Live.
    Faltering,    // Live, heartbeat-miss count >= notResponding and < dead. The
                  // "Unsteady" chip. Presence: Faltering. NEVER entered today.
    Reconnecting, // a transient failure / heartbeat death parked us; a backoff
                  // retry is scheduled (retryAttempt + nextRetryAtMs observable).
                  // Presence: Linking (silent "Connecting…").
    Stale,        // gracefully disconnected OR a self-unpair-less collapse: the
                  // pairing key is KEPT, the chip reads "Needs pairing". Presence:
                  // Stale. (Disconnect lands here, not Failed.)
    Failed,       // terminal: the session failed with a RETAINED typed cause
                  // (`failure`). AuthRejected / VersionMismatch drop the key /
                  // stop retrying. Presence: Stale.
};

// ── Failure reason ──────────────────────────────────────────────────────────
// The typed, RETAINED reason a session failed — the value the old fire-and-forget
// errorMessage() toast threw away. Populated IFF phase == Failed, OR phase ==
// Stale WITH a cause (a server close-notify(unpaired) collapse keeps the row in a
// "Needs pairing" Stale but records why). nullopt otherwise.
enum class SessionFailure {
    Unreachable,        // transport failure / 503 / malformed PUT reply — retryable;
                        // surfaced as the failure on the Reconnecting model and, if
                        // a user-initiated attempt, loudly. Maps from
                        // RestVerdict::Unreachable / ShuttingDown / ServerError.
    VersionMismatch,    // 409 — client/server protocol skew. TERMINAL.
                        // Maps from RestVerdict::VersionMismatch.
    AuthRejected,       // 401 NOT_PAIRED / BAD_PROOF, or no usable key. TERMINAL:
                        // drop the key, re-pair. Maps from RestVerdict::Unauthorized
                        // and from the unpaired close-notify (CloseAction::DropKeyRePair).
    Declined,           // the satellite refused the pair (operator denied / device
                        // rejected). Maps from PairVerdict::AuthRequired on the
                        // forward-pair arm (reachable but no key adopted).
    ServerShuttingDown, // 503 specifically — distinguished from a generic unreachable
                        // so the UI can say "the satellite is restarting"; still
                        // retryable. (Carried separately from Unreachable on the
                        // Reconnecting model.)
    ServerError,        // any other non-2xx with a body — usually retryable.
};

inline bool sessionFailureTerminal(SessionFailure f) {
    return f == SessionFailure::VersionMismatch || f == SessionFailure::AuthRejected ||
           f == SessionFailure::Declined;
}

// ── Connect intent ────────────────────────────────────────────────────────────
// Why a connect attempt was kicked off — gates whether a failure is "loud" (a
// user-facing toast) vs silent (the row chip flip is the only cue). Mirrors
// dish::net::ConnectIntent (UserInitiated / AutoReconnect / RetryAfterDeath),
// folded to two: every non-user origin is silent, so RetryAfterDeath collapses
// into AutoReconnect here.
enum class ConnectIntent {
    UserInitiated, // the user tapped Connect — every failure SHOULD toast (loud).
    AutoReconnect, // app start / the timer / the post-death backoff retry — failure
                   // MUST be silent (the chip's Connecting -> Saved/Stale flip is the cue).
};

// ── Model ─────────────────────────────────────────────────────────────────────
// One satellite session's state. INVARIANTS the reducer upholds:
//   * `failure` is populated IFF phase == Failed, OR phase == Stale with a recorded
//     cause; it is cleared on every successful Live transition and on a fresh Connect.
//   * `retryAttempt` is the 1-based consecutive silent-retry count driving the
//     backoff; it INCREMENTS on each transient failure that schedules a retry and
//     RESETS to 0 on a successful Linked (a live session) and on a fresh
//     UserInitiated Connect.
//   * `nextRetryAtMs` is the absolute wall-clock deadline of the pending retry. It
//     is NOT set by reduce() (clock-free); the coordinator stamps it when it arms
//     the timer from a ScheduleRetry effect. reduce() resets it to 0 whenever it
//     LEAVES a retry (a retry fired, a success, a graceful disconnect, a forget).
//   * `missedHeartbeats` mirrors the alive-poll miss count; HeartbeatOk resets it
//     to 0, HeartbeatMiss{count} records it.
//   * `intent` is the origin of the in-flight attempt; it gates loud-vs-silent
//     Notify and is carried across the Linking/Reconnecting span.
struct SessionModel {
    SessionPhase phase = SessionPhase::Discovered;
    std::optional<SessionFailure> failure; // set iff Failed / Stale-with-cause
    ConnectIntent intent = ConnectIntent::AutoReconnect;
    int retryAttempt = 0;        // 1-based consecutive backoff count
    long long nextRetryAtMs = 0; // absolute deadline; stamped by the coordinator, not reduce()
    int missedHeartbeats = 0;    // last alive-poll miss count

    bool operator==(const SessionModel& o) const {
        return phase == o.phase && failure == o.failure && intent == o.intent &&
               retryAttempt == o.retryAttempt && nextRetryAtMs == o.nextRetryAtMs &&
               missedHeartbeats == o.missedHeartbeats;
    }
    bool operator!=(const SessionModel& o) const { return !(*this == o); }
};

// ── Events ────────────────────────────────────────────────────────────────────

namespace session_event {

// The user / app asked to connect. UserInitiated resets the backoff curve.
struct Connect {
    ConnectIntent intent = ConnectIntent::UserInitiated;
    // Whether a pair handshake is needed first (no usable pairing key yet). When
    // false the coordinator already holds a key and we go straight to the session
    // PUT (Linking); when true we POST /api/pair first (Pairing). Mirrors the
    // manager's credentialsFor()-gated openSession-vs-pairAndConnect split.
    bool needsPair = false;
    bool operator==(const Connect& o) const {
        return intent == o.intent && needsPair == o.needsPair;
    }
    bool operator!=(const Connect& o) const { return !(*this == o); }
};

// The PUT /api/connections reply, ALREADY classified by classifyRest (RestOutcome.h).
struct RestClassified {
    RestVerdict verdict = RestVerdict::Unreachable;
    bool operator==(const RestClassified& o) const { return verdict == o.verdict; }
    bool operator!=(const RestClassified& o) const { return !(*this == o); }
};

// The POST /api/pair reply, ALREADY classified by classifyPair (RestOutcome.h).
struct PairClassified {
    PairVerdict verdict = PairVerdict::Unreachable;
    bool operator==(const PairClassified& o) const { return verdict == o.verdict; }
    bool operator!=(const PairClassified& o) const { return !(*this == o); }
};

// The UDP tuple was bound and the session is live (the session PUT succeeded AND
// the socket opened). Drives Live; resets the backoff + clears any failure.
struct Linked {
    bool operator==(const Linked&) const { return true; }
    bool operator!=(const Linked&) const { return false; }
};

// An alive-poll tick observed `count` consecutive missed heartbeat acks.
// count >= kHeartbeatMissNotResponding && < kHeartbeatMissDead -> Faltering;
// count >= kHeartbeatMissDead -> the session is dead -> Reconnecting + ScheduleRetry.
struct HeartbeatMiss {
    int count = 0;
    bool operator==(const HeartbeatMiss& o) const { return count == o.count; }
    bool operator!=(const HeartbeatMiss& o) const { return !(*this == o); }
};

// An alive-poll tick saw an ack: resets the missed count and leaves Faltering for Live.
struct HeartbeatOk {
    bool operator==(const HeartbeatOk&) const { return true; }
    bool operator!=(const HeartbeatOk&) const { return false; }
};

// An authenticated server close-notify, ALREADY mapped to a follow-up action by
// closeActionForReason (CloseNotify.h). The reducer acts on the CloseAction; it
// does not re-map the raw reason byte.
struct Closed {
    CloseAction action = CloseAction::RetryBackoff;
    bool operator==(const Closed& o) const { return action == o.action; }
    bool operator!=(const Closed& o) const { return !(*this == o); }
};

// The enriched heartbeat ack's epoch/bitmap drifted from applied — the
// reconcile reducer (Reconcile.h) decided a GET-then-maybe-rePUT is warranted.
// While Live we converge with a fresh session PUT (back to Linking + OpenSession);
// the topology diff itself stays in the coordinator (reconcileNeeded /
// appliedMatchesDesired).
struct ReconcileDrift {
    bool operator==(const ReconcileDrift&) const { return true; }
    bool operator!=(const ReconcileDrift&) const { return false; }
};

// The scheduled backoff retry timer fired — re-attempt the session PUT.
struct RetryTimerFired {
    bool operator==(const RetryTimerFired&) const { return true; }
    bool operator!=(const RetryTimerFired&) const { return false; }
};

// The user gracefully disconnected. Lands in Stale (the pairing KEY IS KEPT), not
// Failed — the row reads "Needs pairing" but a single Connect re-links.
struct Disconnect {
    bool operator==(const Disconnect&) const { return true; }
    bool operator!=(const Disconnect&) const { return false; }
};

// The user forgot this satellite. Removes it from tracking (next == nullopt) and
// drops the stored key.
struct Forget {
    bool operator==(const Forget&) const { return true; }
    bool operator!=(const Forget&) const { return false; }
};

} // namespace session_event

using SessionEvent =
    std::variant<session_event::Connect, session_event::RestClassified,
                 session_event::PairClassified, session_event::Linked, session_event::HeartbeatMiss,
                 session_event::HeartbeatOk, session_event::Closed, session_event::ReconcileDrift,
                 session_event::RetryTimerFired, session_event::Disconnect, session_event::Forget>;

// ── Effects (returned as data; executed by the coordinator) ─────────────────

namespace session_effect {

// PUT /api/connections — the declarative session open (identity + proof + full
// topology). The coordinator feeds the reply back as RestClassified, and a bound
// socket as Linked.
struct OpenSession {
    bool operator==(const OpenSession&) const { return true; }
    bool operator!=(const OpenSession&) const { return false; }
};

// POST /api/pair — start a forward pairing handshake. The coordinator feeds the
// reply back as PairClassified.
struct Pair {
    bool operator==(const Pair&) const { return true; }
    bool operator!=(const Pair&) const { return false; }
};

// Open the UDP socket / bind the session tuple from a successful session PUT. The
// coordinator feeds Linked back once the socket is up.
struct BindUdp {
    bool operator==(const BindUdp&) const { return true; }
    bool operator!=(const BindUdp&) const { return false; }
};

// Arm the backoff retry timer for `delayMs` (from backoffDelayMs(retryAttempt)).
// The coordinator stamps nextRetryAtMs = now + delayMs when it arms the timer and
// feeds RetryTimerFired back when it elapses. CLOCK-FREE: reduce() emits the delay,
// not an absolute time.
struct ScheduleRetry {
    int delayMs = 0;
    bool operator==(const ScheduleRetry& o) const { return delayMs == o.delayMs; }
    bool operator!=(const ScheduleRetry& o) const { return !(*this == o); }
};

// Drop the stored pairing key (terminal auth failure / unpaired close / forget).
struct DropKey {
    bool operator==(const DropKey&) const { return true; }
    bool operator!=(const DropKey&) const { return false; }
};

// Surface the failure to the user. `loud` is true only when intent == UserInitiated
// (a toast); a silent failure relies on the row chip flip.
struct Notify {
    SessionFailure reason = SessionFailure::Unreachable;
    bool loud = false;
    bool operator==(const Notify& o) const { return reason == o.reason && loud == o.loud; }
    bool operator!=(const Notify& o) const { return !(*this == o); }
};

// Clear any retained failure on the visible row (a fresh attempt / a live session).
struct ClearFailure {
    bool operator==(const ClearFailure&) const { return true; }
    bool operator!=(const ClearFailure&) const { return false; }
};

// Start the alive-poll heartbeat loop (on going Live).
struct StartHeartbeat {
    bool operator==(const StartHeartbeat&) const { return true; }
    bool operator!=(const StartHeartbeat&) const { return false; }
};

// Stop the alive-poll heartbeat loop (on leaving a live/faltering session).
struct StopHeartbeat {
    bool operator==(const StopHeartbeat&) const { return true; }
    bool operator!=(const StopHeartbeat&) const { return false; }
};

} // namespace session_effect

using SessionEffect =
    std::variant<session_effect::OpenSession, session_effect::Pair, session_effect::BindUdp,
                 session_effect::ScheduleRetry, session_effect::DropKey, session_effect::Notify,
                 session_effect::ClearFailure, session_effect::StartHeartbeat,
                 session_effect::StopHeartbeat>;

// The reduction result, mirroring UsbPathMachine.h's
// `Reduction { std::optional<State> next; std::vector<Effect> effects; }` shape:
// a (possibly cleared) next model plus the effects to run AS DATA.
//   next == nullopt means "remove this session from tracking" (a Forget).
// NAMED SessionReduction (not the bare `Reduction` UsbPathMachine uses) on
// purpose: both headers live in namespace dish::reducer, and the
// AppModel/AppViewModel layer aggregates BOTH the USB subsystem
// (UsbGamepadManager.h -> UsbPathMachine.h, which declares its own
// `struct Reduction`) and the connection subsystem (which will own the session
// coordinator that includes this header). A second `struct Reduction` in the same
// namespace would be an ODR redefinition the moment one TU pulls both, so this
// carries a distinct name. The `reduce(const SessionModel&, const SessionEvent&)`
// entry point below is a clean OVERLOAD of the USB `reduce` (distinct parameter
// types — no clash), so the reducer API the prompt asked for is preserved.
struct SessionReduction {
    std::optional<SessionModel> next;
    std::vector<SessionEffect> effects;
};

// ── SatelliteLinkState compatibility ──────────────────────────────────────────
// Collapse a SessionPhase onto the SatelliteLinkState SessionPresence axis (see
// the file-header table). Pulled out as a pure predicate so the coordinator and
// the UI map identically. The coordinator additionally sets the out-of-band
// `isStale` marker for Stale/Failed (both fold to "Needs pairing" through
// satelliteLinkState's Idle arm; Stale presence itself also maps to UiLinkState::Stale).
inline SessionPresence sessionPhaseToPresence(SessionPhase phase) {
    switch (phase) {
    case SessionPhase::Live:
        return SessionPresence::Live;
    case SessionPhase::Faltering:
        return SessionPresence::Faltering;
    case SessionPhase::Pairing:
    case SessionPhase::Linking:
    case SessionPhase::Reconnecting:
        // A silent backoff retry reads "Connecting…", matching the manager's
        // markStale->Linking chip cue today.
        return SessionPresence::Linking;
    case SessionPhase::Stale:
    case SessionPhase::Failed:
        return SessionPresence::Stale;
    case SessionPhase::Discovered:
    default:
        return SessionPresence::Idle;
    }
}

// Whether the coordinator should set the out-of-band "Needs pairing" stale marker
// for this phase (the isStale arg to satelliteLinkState). Failed and Stale both
// surface "Needs pairing"; everything else is false.
inline bool sessionPhaseIsStaleMarker(SessionPhase phase) {
    return phase == SessionPhase::Stale || phase == SessionPhase::Failed;
}

// ── Reducer ─────────────────────────────────────────────────────────────────
// The total session reducer: (model, event) -> next model + effects. Defined for
// EVERY (phase x event); never throws; reads no clock. The rules, by event:
//
//   Connect{intent, needsPair}
//     From any non-live phase, starts a fresh attempt:
//       needsPair == true  -> Pairing + [ClearFailure, Pair].
//       needsPair == false -> Linking + [ClearFailure, OpenSession].
//     A UserInitiated Connect RESETS retryAttempt to 0 (a user tap restarts the
//     curve); an AutoReconnect Connect preserves the running retryAttempt (it is
//     the backoff curve continuing). The prior failure is cleared and the new
//     intent adopted. From Live/Faltering a Connect is a no-op (already connected;
//     the manager's connectTo returns early when Live/Linking) — we keep the
//     session and emit nothing.
//
//   PairClassified{verdict}  — only meaningful while Pairing:
//     Success      -> Linking + [OpenSession] (key adopted; open the session).
//     Pending      -> stay Pairing (Path-B style approval poll continues elsewhere).
//     AuthRequired -> Failed(Declined) + [Notify(Declined, loud?)] (reachable but
//                     no usable key — the pair was refused / needs a PIN).
//     VersionMismatch -> Failed(VersionMismatch) + [Notify(VersionMismatch, loud?)].
//     Unreachable  -> Reconnecting + [ScheduleRetry(backoff), Notify(Unreachable, loud?)]
//                     with retryAttempt incremented (a transient pair failure rides
//                     the backoff like a transient session failure).
//     In any non-Pairing phase a PairClassified is a stale reply — ignored.
//
//   RestClassified{verdict}  — only meaningful while Linking / Reconnecting:
//     Ok            -> stay in phase (session PUT accepted; the socket bind / Linked
//                      event drives Live next — Ok alone is not "live", mirroring the
//                      manager's openSession which still must open the UDP socket).
//     Unauthorized  -> Failed(AuthRejected) + [DropKey, Notify(AuthRejected, loud?)].
//                      TERMINAL: drop the key, stop retrying.
//     VersionMismatch -> Failed(VersionMismatch) + [Notify(VersionMismatch, loud?)]. TERMINAL.
//     ShuttingDown  -> Reconnecting + [ScheduleRetry(backoff), Notify(ServerShuttingDown, loud?)],
//                      retryAttempt incremented.
//     ServerError   -> Reconnecting + [ScheduleRetry(backoff), Notify(ServerError, loud?)],
//                      retryAttempt incremented.
//     Unreachable   -> Reconnecting + [ScheduleRetry(backoff), Notify(Unreachable, loud?)],
//                      retryAttempt incremented.
//     In any other phase a RestClassified is a stale reply — ignored.
//
//   Linked   — the session is live:
//     From Linking / Reconnecting / Pairing -> Live, retryAttempt RESET to 0,
//       failure CLEARED, missedHeartbeats RESET, nextRetryAtMs cleared, with
//       [ClearFailure, StartHeartbeat]. This is the ONLY path to Live, and the
//       single place the backoff curve is reset on success.
//     From Live / Faltering a re-Linked re-asserts Live (resets misses) — idempotent.
//     From a settled phase (Discovered / Stale / Failed) a stray Linked is ignored.
//
//   HeartbeatMiss{count}  — only meaningful while Live / Faltering:
//     count >= kHeartbeatMissDead -> the session is DEAD -> Reconnecting +
//       [StopHeartbeat, ScheduleRetry(backoff)], retryAttempt incremented,
//       missedHeartbeats recorded. (Death is always a silent retry — there is no
//       user tap behind a heartbeat death — so the Notify here is silent; we emit
//       none, matching the manager's onDead -> disconnect + scheduleRetry(RetryAfterDeath).)
//     count >= kHeartbeatMissNotResponding -> Faltering (records the miss count). No
//       effects (the socket stays up; we are merely degraded). THIS IS THE BUG FIX:
//       today this transition is computed nowhere.
//     count <  kHeartbeatMissNotResponding -> just record the count, stay in phase.
//     In any non-live phase a HeartbeatMiss is ignored.
//
//   HeartbeatOk  — only meaningful while Live / Faltering:
//     Resets missedHeartbeats to 0 and, from Faltering, returns to Live (the link
//     recovered). From Live it is a no-op beyond the reset. Ignored elsewhere.
//
//   Closed{action}  — an authenticated close-notify (mapped via closeActionForReason):
//     DropKeyRePair -> Stale(failure = AuthRejected) + [StopHeartbeat, DropKey]
//                      (unpaired: trust revoked — keep the row but mark it, drop the
//                      key, STOP retrying). Note: lands in STALE with a recorded
//                      cause, not Failed, matching the manager's markStale on unpaired.
//     StayDown      -> Stale + [StopHeartbeat] (replaced: a newer PUT owns the
//                      session — do nothing further; key kept).
//     RetryBackoff  -> Reconnecting + [StopHeartbeat, ScheduleRetry(backoff)],
//                      retryAttempt incremented (shutdown / kicked: transient).
//     In a non-live phase a Closed is ignored (no live session to tear down).
//
//   ReconcileDrift  — only meaningful while Live / Faltering:
//     -> Linking + [OpenSession] (converge with a fresh session PUT; the UDP tuple
//        is rotated by the PUT). Mirrors the manager's reconcile -> markDisconnected
//        -> markConnecting -> openSession. retryAttempt is preserved (not a failure).
//     Ignored in any non-live phase.
//
//   RetryTimerFired  — only meaningful while Reconnecting:
//     -> Linking + [OpenSession], nextRetryAtMs cleared. retryAttempt is PRESERVED
//        (it is the running count; it only resets on Linked or a UserInitiated Connect).
//     Ignored in any other phase (a user reconnect / forget moved us out).
//
//   Disconnect  — graceful, from any phase:
//     -> Stale (the pairing KEY IS KEPT — failure cleared, retry state cleared) +
//        [StopHeartbeat]. NOT Failed: the row reads "Needs pairing" but a single
//        Connect re-links. Total and unconditional.
//
//   Forget  — from any phase:
//     -> next == nullopt (removed from tracking) + [StopHeartbeat, DropKey].
//        Total and unconditional.
//
// Anything not named above for a given phase is a no-op (returns the model
// unchanged with no effects), making every combination explicit.
inline SessionReduction reduce(const SessionModel& s, const SessionEvent& event) {
    using namespace session_event;
    namespace fx = session_effect;

    const bool live = s.phase == SessionPhase::Live || s.phase == SessionPhase::Faltering;
    const bool loud = s.intent == ConnectIntent::UserInitiated;

    // Build a Reconnecting model from a transient failure: increment the backoff
    // count, record the typed cause, schedule a retry at backoffDelayMs(attempt),
    // and (if user-initiated) notify loudly. nextRetryAtMs stays 0 here — the
    // coordinator stamps it when it arms the timer (clock-free reduce()).
    auto toReconnecting = [&](SessionFailure cause, bool emitNotify) -> SessionReduction {
        SessionModel next = s;
        next.phase = SessionPhase::Reconnecting;
        next.retryAttempt = s.retryAttempt + 1;
        next.failure = cause;
        next.nextRetryAtMs = 0;
        const int delay = static_cast<int>(backoffDelayMs(next.retryAttempt));
        SessionReduction r;
        r.next = next;
        if (live) { r.effects.push_back(fx::StopHeartbeat{}); }
        r.effects.push_back(fx::ScheduleRetry{delay});
        if (emitNotify) { r.effects.push_back(fx::Notify{cause, loud}); }
        return r;
    };

    // Build a terminal Failed model with a retained cause + a notify.
    auto toFailed = [&](SessionFailure cause, bool dropKey) -> SessionReduction {
        SessionModel next = s;
        next.phase = SessionPhase::Failed;
        next.failure = cause;
        next.nextRetryAtMs = 0;
        SessionReduction r;
        r.next = next;
        if (live) { r.effects.push_back(fx::StopHeartbeat{}); }
        if (dropKey) { r.effects.push_back(fx::DropKey{}); }
        r.effects.push_back(fx::Notify{cause, loud});
        return r;
    };

    return std::visit(
        [&](const auto& e) -> SessionReduction {
            using E = std::decay_t<decltype(e)>;

            // ── Connect: start / restart an attempt ──────────────────────────
            if constexpr (std::is_same_v<E, Connect>) {
                // Already connected (or connecting to live) — the manager's
                // connectTo returns early when Live/Linking; keep the session.
                if (live) { return SessionReduction{s, {}}; }
                SessionModel next = s;
                next.intent = e.intent;
                next.failure = std::nullopt;
                next.nextRetryAtMs = 0;
                next.missedHeartbeats = 0;
                // A user tap restarts the backoff curve; an auto-reconnect Connect
                // is the curve continuing, so it preserves the count.
                if (e.intent == ConnectIntent::UserInitiated) { next.retryAttempt = 0; }
                SessionReduction r;
                if (e.needsPair) {
                    next.phase = SessionPhase::Pairing;
                    r.next = next;
                    r.effects = {fx::ClearFailure{}, fx::Pair{}};
                } else {
                    next.phase = SessionPhase::Linking;
                    r.next = next;
                    r.effects = {fx::ClearFailure{}, fx::OpenSession{}};
                }
                return r;
            }

            // ── PairClassified: map the pair verdict, only while Pairing ──────
            else if constexpr (std::is_same_v<E, PairClassified>) {
                if (s.phase != SessionPhase::Pairing) { return SessionReduction{s, {}}; }
                switch (e.verdict) {
                case PairVerdict::Success: {
                    // Key adopted — open the session.
                    SessionModel next = s;
                    next.phase = SessionPhase::Linking;
                    return SessionReduction{next, {fx::OpenSession{}}};
                }
                case PairVerdict::Pending:
                    // Path-B style staged grant; the approval poll continues
                    // outside this machine. Keep waiting.
                    return SessionReduction{s, {}};
                case PairVerdict::AuthRequired:
                    // Reachable but no usable key adopted — the pair was refused.
                    return toFailed(SessionFailure::Declined, /*dropKey=*/false);
                case PairVerdict::VersionMismatch:
                    return toFailed(SessionFailure::VersionMismatch, /*dropKey=*/false);
                case PairVerdict::Unreachable:
                    return toReconnecting(SessionFailure::Unreachable, /*emitNotify=*/true);
                }
                return SessionReduction{s, {}}; // defensive: unknown verdict — keep waiting
            }

            // ── RestClassified: map the session-PUT verdict ──────────────────
            else if constexpr (std::is_same_v<E, RestClassified>) {
                if (s.phase != SessionPhase::Linking && s.phase != SessionPhase::Reconnecting) {
                    return SessionReduction{s, {}}; // stale reply for a settled attempt
                }
                switch (e.verdict) {
                case RestVerdict::Ok:
                    // PUT accepted; the socket bind / Linked event drives Live.
                    // Ok alone is not "live" (the UDP socket still must open).
                    return SessionReduction{s, {}};
                case RestVerdict::Unauthorized:
                    return toFailed(SessionFailure::AuthRejected, /*dropKey=*/true);
                case RestVerdict::VersionMismatch:
                    return toFailed(SessionFailure::VersionMismatch, /*dropKey=*/false);
                case RestVerdict::ShuttingDown:
                    return toReconnecting(SessionFailure::ServerShuttingDown, /*emitNotify=*/true);
                case RestVerdict::ServerError:
                    return toReconnecting(SessionFailure::ServerError, /*emitNotify=*/true);
                case RestVerdict::Unreachable:
                    return toReconnecting(SessionFailure::Unreachable, /*emitNotify=*/true);
                }
                return SessionReduction{s, {}}; // defensive
            }

            // ── Linked: the ONLY path to Live; resets the backoff ────────────
            else if constexpr (std::is_same_v<E, Linked>) {
                if (s.phase == SessionPhase::Discovered || s.phase == SessionPhase::Stale ||
                    s.phase == SessionPhase::Failed) {
                    return SessionReduction{s, {}}; // stray confirmation — ignore
                }
                SessionModel next = s;
                next.phase = SessionPhase::Live;
                next.failure = std::nullopt;
                next.retryAttempt = 0; // success resets the backoff curve
                next.nextRetryAtMs = 0;
                next.missedHeartbeats = 0;
                return SessionReduction{next, {fx::ClearFailure{}, fx::StartHeartbeat{}}};
            }

            // ── HeartbeatMiss: drive Faltering (notResponding) / death (dead) ─
            else if constexpr (std::is_same_v<E, HeartbeatMiss>) {
                if (!live) { return SessionReduction{s, {}}; }
                SessionModel next = s;
                next.missedHeartbeats = e.count;
                if (e.count >= kHeartbeatMissDead) {
                    // Dead — silent backoff retry (no user tap behind a death).
                    next.phase = SessionPhase::Reconnecting;
                    next.retryAttempt = s.retryAttempt + 1;
                    next.failure = SessionFailure::Unreachable;
                    next.nextRetryAtMs = 0;
                    const int delay = static_cast<int>(backoffDelayMs(next.retryAttempt));
                    return SessionReduction{next, {fx::StopHeartbeat{}, fx::ScheduleRetry{delay}}};
                }
                if (e.count >= kHeartbeatMissNotResponding) {
                    // Degraded but still up — THE BUG FIX: Faltering is finally entered.
                    next.phase = SessionPhase::Faltering;
                    return SessionReduction{next, {}};
                }
                // Below the threshold — just record the count, stay in phase.
                return SessionReduction{next, {}};
            }

            // ── HeartbeatOk: reset misses; recover from Faltering ────────────
            else if constexpr (std::is_same_v<E, HeartbeatOk>) {
                if (!live) { return SessionReduction{s, {}}; }
                SessionModel next = s;
                next.missedHeartbeats = 0;
                if (s.phase == SessionPhase::Faltering) { next.phase = SessionPhase::Live; }
                return SessionReduction{next, {}};
            }

            // ── Closed: act on the close-notify follow-up action ─────────────
            else if constexpr (std::is_same_v<E, Closed>) {
                if (!live) { return SessionReduction{s, {}}; }
                switch (e.action) {
                case CloseAction::DropKeyRePair: {
                    // unpaired: trust revoked — park Stale WITH the cause, drop key.
                    SessionModel next = s;
                    next.phase = SessionPhase::Stale;
                    next.failure = SessionFailure::AuthRejected;
                    next.retryAttempt = 0;
                    next.nextRetryAtMs = 0;
                    next.missedHeartbeats = 0;
                    return SessionReduction{next, {fx::StopHeartbeat{}, fx::DropKey{}}};
                }
                case CloseAction::StayDown: {
                    // replaced: a newer PUT owns the session — key kept, nothing more.
                    SessionModel next = s;
                    next.phase = SessionPhase::Stale;
                    next.failure = std::nullopt;
                    next.retryAttempt = 0;
                    next.nextRetryAtMs = 0;
                    next.missedHeartbeats = 0;
                    return SessionReduction{next, {fx::StopHeartbeat{}}};
                }
                case CloseAction::RetryBackoff:
                    // shutdown / kicked: transient — reconnect on the backoff curve.
                    return toReconnecting(SessionFailure::Unreachable, /*emitNotify=*/false);
                }
                return SessionReduction{s, {}}; // defensive
            }

            // ── ReconcileDrift: converge with a fresh session PUT ────────────
            else if constexpr (std::is_same_v<E, ReconcileDrift>) {
                if (!live) { return SessionReduction{s, {}}; }
                SessionModel next = s;
                next.phase = SessionPhase::Linking;
                next.missedHeartbeats = 0;
                // Not a failure: preserve retryAttempt + clear any stale failure.
                next.failure = std::nullopt;
                return SessionReduction{next, {fx::StopHeartbeat{}, fx::OpenSession{}}};
            }

            // ── RetryTimerFired: re-attempt the session PUT ──────────────────
            else if constexpr (std::is_same_v<E, RetryTimerFired>) {
                if (s.phase != SessionPhase::Reconnecting) { return SessionReduction{s, {}}; }
                SessionModel next = s;
                next.phase = SessionPhase::Linking;
                next.nextRetryAtMs = 0; // the pending deadline is consumed
                // retryAttempt is PRESERVED — it is the running count.
                return SessionReduction{next, {fx::OpenSession{}}};
            }

            // ── Disconnect: graceful — Stale, KEY KEPT ───────────────────────
            else if constexpr (std::is_same_v<E, Disconnect>) {
                SessionModel next = s;
                next.phase = SessionPhase::Stale;
                next.failure = std::nullopt; // graceful — no failure cause
                next.retryAttempt = 0;
                next.nextRetryAtMs = 0;
                next.missedHeartbeats = 0;
                SessionReduction r;
                r.next = next;
                if (live) { r.effects.push_back(fx::StopHeartbeat{}); }
                return r;
            }

            // ── Forget: remove from tracking + drop the key ──────────────────
            else if constexpr (std::is_same_v<E, Forget>) {
                SessionReduction r;
                r.next = std::nullopt; // removed from tracking
                if (live) { r.effects.push_back(fx::StopHeartbeat{}); }
                r.effects.push_back(fx::DropKey{});
                return r;
            }

            // ── Total fallback (no event type reaches here) ──────────────────
            else {
                return SessionReduction{s, {}};
            }
        },
        event);
}

} // namespace dish::reducer
