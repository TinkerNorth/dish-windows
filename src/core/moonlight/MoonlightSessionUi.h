// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What the Moonlight session section renders, as a pure total function of what
// is known about the host. The sibling of MoonlightSessionMachine: that one owns
// the wire lifecycle, this one owns the twenty-one states a user can be looking
// at, so the binding flow never re-derives a state from a phase.
//
// Moonlight has no bidirectional liveness. Pairing is remembered trust, checked
// only when we ask, so every input here is the result of a probe we ran rather
// than something the host told us. `hostSessionActive` and `appCount` come from
// a MUTUAL-TLS probe: a plaintext /serverinfo always reports the host free, and
// a session another device holds is discovered only by attempting /launch.
//
// Exactly one state renders at a time. The states are listed in reading order;
// the resolver settles the ones that overlap first (a PIN on screen outranks
// "not paired yet", a host carrying four pads outranks "join this session"), so
// each state has one trigger and the answer does not depend on how the caller
// asks.
//
// No IO, no Qt. QML localizes from the token; nothing here is a sentence.

#pragma once

#include "core/moonlight/MoonlightPadSlots.h"
#include "core/moonlight/MoonlightSessionMachine.h"

namespace dish::moonlight {

enum class SessionUiState {
    Checking,          // M1  probe in flight, nothing cached
    NotPaired,         // M2  answered, PairStatus 0, no stored server cert
    PairingPin,        // M3  pairing in flight, the PIN is on screen
    PairRefused,       // M4  pairing finished not-ok
    Unreachable,       // M5  never paired, no answer
    RememberedOffline, // M6  remembered, no answer
    TrustLost,         // M7  answered with PairStatus 0 over a stored cert, or 401
    HostReplaced,      // M8  the host's uniqueid is not the remembered one
    AppsLoading,       // M9  paired, no session of ours, /applist in flight
    NewSession,        // M10 paired, no session of ours, list non-empty
    NoApps,            // M11 list fetched, empty
    AppsUnreadable,    // M12 /applist failed while paired
    JoiningSession,    // M13 this device already holds a session on this host
    HostFull,          // M14 four controllers already bound to this host
    BusyOther,         // M15 refused, the session belongs to another device
    RejoinRefused,     // M16 the host offered a resume and then would not give it
    Refused,           // M17 refused for a reason of the host's own
    SetupFailed,       // M18 launched, then the stream did not come up
    Live,              // M19 control stream connected
    Dropped,           // M20 was live, the link closed without a host termination
    EndedByHost,       // M21 the host terminated, or the app closed
};

// How the last attempt on this host ended. None means nothing has been tried
// this visit, which is what separates M9 through M13 from everything below them:
// "no session of ours" and "the host refused" are both true after a refusal, and
// only the refusal is worth rendering.
enum class SessionOutcome {
    None,
    BusyOther,
    RejoinRefused,
    Refused,
    SetupFailed,
    Live,
    Dropped,
    EndedByHost,
};

// The three words the hosts screen renders. Never a liveness light: the host
// cannot tell us it has forgotten us, so trust is remembered and verified late.
enum class TrustState {
    NotPaired,
    Remembered,
    Paired,
};

struct SessionUiInputs {
    bool probeInFlight = false;
    bool probeAnswered = false;
    bool probeTimedOut = false;
    // PairStatus 1 on the probe that answered.
    bool hostPairStatus = false;
    bool serverCertStored = false;
    // Any mutual-TLS call came back 401.
    bool unauthorized = false;
    // /serverinfo named a uniqueid that is not the remembered one.
    bool uniqueIdChanged = false;
    bool pairingActive = false;
    bool pairingRefused = false;
    bool appsInFlight = false;
    bool appsFetched = false;
    bool appsFailed = false;
    int appCount = 0;
    // This device holds a session on this host that a new binding would join.
    bool hostSessionActive = false;
    int boundControllers = 0;
    SessionOutcome outcome = SessionOutcome::None;
};

inline SessionUiState resolveSessionUi(const SessionUiInputs& in) {
    if (in.pairingActive) { return SessionUiState::PairingPin; }
    if (in.pairingRefused) { return SessionUiState::PairRefused; }
    if (in.uniqueIdChanged) { return SessionUiState::HostReplaced; }
    if (in.unauthorized) { return SessionUiState::TrustLost; }
    if (in.probeTimedOut) {
        return in.serverCertStored ? SessionUiState::RememberedOffline
                                   : SessionUiState::Unreachable;
    }
    if (in.probeAnswered && !in.hostPairStatus) {
        return in.serverCertStored ? SessionUiState::TrustLost : SessionUiState::NotPaired;
    }
    if (!in.probeAnswered) { return SessionUiState::Checking; }

    if (in.boundControllers >= static_cast<int>(kMaxPads)) { return SessionUiState::HostFull; }

    switch (in.outcome) {
    case SessionOutcome::Live:
        return SessionUiState::Live;
    case SessionOutcome::Dropped:
        return SessionUiState::Dropped;
    case SessionOutcome::EndedByHost:
        return SessionUiState::EndedByHost;
    case SessionOutcome::BusyOther:
        return SessionUiState::BusyOther;
    case SessionOutcome::RejoinRefused:
        return SessionUiState::RejoinRefused;
    case SessionOutcome::Refused:
        return SessionUiState::Refused;
    case SessionOutcome::SetupFailed:
        return SessionUiState::SetupFailed;
    case SessionOutcome::None:
        break;
    }

    if (in.hostSessionActive) { return SessionUiState::JoiningSession; }
    if (in.appsInFlight) { return SessionUiState::AppsLoading; }
    if (in.appsFailed) { return SessionUiState::AppsUnreadable; }
    if (in.appsFetched) {
        return in.appCount > 0 ? SessionUiState::NewSession : SessionUiState::NoApps;
    }
    return SessionUiState::Checking;
}

// A binding is a durable intent and pairing is verified lazily, so the session
// is attempted when the controller is used and not when the binding is saved.
// The one exception is a host already carrying its four controllers, which is a
// protocol ceiling rather than a state that will resolve itself.
inline bool sessionUiBlocksApply(SessionUiState state) { return state == SessionUiState::HostFull; }

// Whether the state offers to close whatever the host is running. The only two
// destructive actions in the flow, and both re-probe afterwards because /cancel
// answers 200 whether or not anything was running.
inline bool sessionUiOffersQuit(SessionUiState state) {
    return state == SessionUiState::BusyOther || state == SessionUiState::RejoinRefused ||
           state == SessionUiState::Live;
}

// Amber is the problem colour, never the working one: a state that is simply in
// progress reads neutral.
inline bool sessionUiIsProblem(SessionUiState state) {
    switch (state) {
    case SessionUiState::Checking:
    case SessionUiState::PairingPin:
    case SessionUiState::AppsLoading:
    case SessionUiState::NewSession:
    case SessionUiState::JoiningSession:
    case SessionUiState::Live:
        return false;
    default:
        return true;
    }
}

// The wire lifecycle as the one thing the section renders from it. A phase that
// is still on its way to Streaming is not an outcome: nothing has happened yet
// that the user has to answer.
inline SessionOutcome sessionOutcomeFor(const SessionState& state) {
    if (state.phase == SessionPhase::Streaming || state.phase == SessionPhase::Faltering) {
        return SessionOutcome::Live;
    }
    if (state.phase != SessionPhase::Failed) { return SessionOutcome::None; }
    switch (state.failure) {
    case SessionFailure::AppAlreadyRunning:
        return SessionOutcome::BusyOther;
    case SessionFailure::ResumeRejected:
        return SessionOutcome::RejoinRefused;
    case SessionFailure::LaunchRejected:
        return SessionOutcome::Refused;
    case SessionFailure::RtspFailed:
    case SessionFailure::ControlFailed:
        return SessionOutcome::SetupFailed;
    case SessionFailure::LinkDropped:
        return SessionOutcome::Dropped;
    case SessionFailure::ServerTerminated:
        return SessionOutcome::EndedByHost;
    case SessionFailure::None:
    case SessionFailure::PairRejected:
    case SessionFailure::Unreachable:
        return SessionOutcome::None;
    }
    return SessionOutcome::None;
}

inline TrustState trustFor(const SessionUiInputs& in) {
    if (in.uniqueIdChanged || in.unauthorized) { return TrustState::NotPaired; }
    if (in.probeAnswered) { return in.hostPairStatus ? TrustState::Paired : TrustState::NotPaired; }
    return in.serverCertStored ? TrustState::Remembered : TrustState::NotPaired;
}

} // namespace dish::moonlight
