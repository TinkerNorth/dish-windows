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
// than something the host told us. `sessionLive` and `appCount` come from
// a MUTUAL-TLS probe: a plaintext /serverinfo always reports the host free, and
// a session another device holds is discovered only by attempting /launch.
//
// Exactly one state renders at a time. The states are listed in reading order;
// the resolver settles the ones that overlap first (a PIN on screen outranks
// "not paired yet", a host carrying four pads outranks everything), so each
// state has one trigger and the answer does not depend on how the caller asks.
//
// No IO, no Qt. QML localizes from the token; nothing here is a sentence.

#pragma once

#include "core/moonlight/MoonlightPadSlots.h"
#include "core/moonlight/MoonlightSessionMachine.h"

namespace dish::moonlight {

enum class SessionUiState {
    Checking,       // M1  probe in flight, nothing cached
    NotPaired,      // M2  answered, PairStatus 0, no stored server cert
    PairingPin,     // M3  pairing in flight, the PIN is on screen
    PairingRefused, // M4  pairing finished not-ok
    Unreachable,    // M5  never answered, and nothing remembered
    Remembered,     // M6  never answered, but the pairing is remembered
    TrustLost,      // M7  answered with PairStatus 0 over a stored cert, or a 401
    HostReplaced,   // M8  the host's uniqueid, or its certificate, is not ours
    AppsLoading,    // M9  paired, no session of ours, /applist in flight
    NewSession,     // M10 paired, no session of ours, the list is readable
    NoApps,         // M11 the list came back empty
    AppsFailed,     // M12 /applist failed while paired
    Joining,        // M13 this device already holds a session on this host
    HostFull,       // M14 four controllers already ride this host
    BusyOther,      // M15 refused, the session belongs to another device
    ResumeFailed,   // M16 resume was offered, then would not hand the session back
    Refused,        // M17 refused for a reason of the host's own
    SetupFailed,    // M18 launched, then the stream did not come up
    Live,           // M19 this binding is on a connected control stream
    Dropped,        // M20 was live, the link closed without a host termination
    EndedByHost,    // M21 the host terminated, or the app closed
};

// How the last attempt on this host ended. None means nothing has been tried
// this visit, which is what separates M9 through M13 from everything below them:
// "no session of ours" and "the host refused" are both true after a refusal, and
// only the refusal is worth rendering.
enum class SessionOutcome {
    None,
    BusyOther,
    ResumeFailed,
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
    // A mutual-TLS call came back 401. Trust lost only where a certificate is
    // stored: a host we have never paired with refuses exactly the same way.
    bool unauthorized = false;
    // /serverinfo named a uniqueid that is not the remembered one.
    bool uniqueIdChanged = false;
    // The host presented a server certificate that is not the pinned one. The
    // second witness for the same fact, and the one that arrives FIRST: the pin
    // is checked during a TLS handshake, and the uniqueid only once a plaintext
    // probe answers. Without it the refused handshake reads as an unreachable
    // host, which sends the user looking at their network for a trust problem.
    bool serverCertChanged = false;
    bool pairingActive = false;
    bool pairingRefused = false;
    bool appsInFlight = false;
    bool appsFetched = false;
    bool appsFailed = false;
    int appCount = 0;
    // This device holds a session on this host. `bindingLive` narrows that to
    // the binding being looked at, which is what separates joining a session
    // from riding one.
    bool sessionLive = false;
    bool bindingLive = false;
    int boundControllers = 0;
    SessionOutcome outcome = SessionOutcome::None;
};

namespace detail {

inline SessionUiState outcomeState(SessionOutcome outcome) {
    switch (outcome) {
    case SessionOutcome::BusyOther:
        return SessionUiState::BusyOther;
    case SessionOutcome::ResumeFailed:
        return SessionUiState::ResumeFailed;
    case SessionOutcome::Refused:
        return SessionUiState::Refused;
    case SessionOutcome::SetupFailed:
        return SessionUiState::SetupFailed;
    case SessionOutcome::Dropped:
        return SessionUiState::Dropped;
    case SessionOutcome::EndedByHost:
        return SessionUiState::EndedByHost;
    case SessionOutcome::None:
    case SessionOutcome::Live:
        break;
    }
    return SessionUiState::Checking;
}

} // namespace detail

inline SessionUiState sessionUiState(const SessionUiInputs& in) {
    if (in.pairingActive) { return SessionUiState::PairingPin; }
    if (in.pairingRefused) { return SessionUiState::PairingRefused; }
    if (in.uniqueIdChanged || in.serverCertChanged) { return SessionUiState::HostReplaced; }

    // The full host is judged FIRST, before anything the network could change,
    // because it is the one state derived entirely from local bookkeeping and the
    // one state that blocks Apply. Rendering a spinner or an unreachable host
    // over it would enable an Apply the bind is going to refuse.
    if (in.boundControllers >= static_cast<int>(kMaxPads)) { return SessionUiState::HostFull; }

    // A live session is its own proof that the host is there, so a probe that has
    // not answered yet cannot draw a spinner over it.
    if (!in.probeAnswered && !in.sessionLive) {
        if (!in.probeTimedOut) { return SessionUiState::Checking; }
        if (in.outcome != SessionOutcome::None) { return detail::outcomeState(in.outcome); }
        return in.serverCertStored ? SessionUiState::Remembered : SessionUiState::Unreachable;
    }

    // A 401 is trust LOST only where there was trust: a host we have never
    // paired with refuses the same way and is simply not paired.
    if (in.unauthorized && in.serverCertStored) { return SessionUiState::TrustLost; }

    // TRUST IS MUTUAL AND THIS CLIENT HOLDS ONE HALF OF IT. A host reports
    // PairStatus against the uniqueid on the request, and this client's uniqueid
    // outlives a forget, so a box that still has this install on file answers 1
    // to a client that has thrown its half away. That is the host's half only:
    // every paired-only call is mutual TLS pinned against the certificate the
    // pairing handshake stored, and with no certificate there is nothing to pin,
    // no app list and no session. So a host we cannot open a channel to is NOT
    // PAIRED however warmly it answers, and the way back in is the same PIN a
    // stranger needs.
    //
    // Reading the host's word alone is what strands the user: it renders a
    // relationship nothing in the client can use, and the one control that
    // repairs it is hidden on exactly that word.
    //
    // Answered-unpaired WITH a certificate stored is the other thing entirely,
    // and keeps its own state: something was lost there.
    if (in.probeAnswered && !(in.hostPairStatus && in.serverCertStored)) {
        return in.serverCertStored ? SessionUiState::TrustLost : SessionUiState::NotPaired;
    }

    if (in.bindingLive) { return SessionUiState::Live; }
    if (in.outcome != SessionOutcome::None && in.outcome != SessionOutcome::Live) {
        return detail::outcomeState(in.outcome);
    }
    if (in.sessionLive) { return SessionUiState::Joining; }

    if (in.appsInFlight) { return SessionUiState::AppsLoading; }
    if (in.appsFailed) { return SessionUiState::AppsFailed; }
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
    return state == SessionUiState::BusyOther || state == SessionUiState::ResumeFailed ||
           state == SessionUiState::Live;
}

// Amber is the problem colour, never the working one. A state that is in
// progress reads neutral, and so does one whose next step is simply the next
// step: a host nobody has paired yet is not a fault to report.
inline bool sessionUiIsProblem(SessionUiState state) {
    switch (state) {
    case SessionUiState::PairingRefused:
    case SessionUiState::Unreachable:
    case SessionUiState::Remembered:
    case SessionUiState::TrustLost:
    case SessionUiState::HostReplaced:
    case SessionUiState::HostFull:
    case SessionUiState::BusyOther:
    case SessionUiState::ResumeFailed:
    case SessionUiState::Refused:
    case SessionUiState::SetupFailed:
    case SessionUiState::Dropped:
    case SessionUiState::EndedByHost:
        return true;
    case SessionUiState::Checking:
    case SessionUiState::NotPaired:
    case SessionUiState::PairingPin:
    case SessionUiState::AppsLoading:
    case SessionUiState::NewSession:
    case SessionUiState::NoApps:
    case SessionUiState::AppsFailed:
    case SessionUiState::Joining:
    case SessionUiState::Live:
        return false;
    }
    return false;
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
        return SessionOutcome::ResumeFailed;
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
    if (in.uniqueIdChanged || in.serverCertChanged || in.unauthorized) {
        return TrustState::NotPaired;
    }
    // BOTH HALVES, for the reason sessionUiState gives above: the host's word
    // that it knows us, and a certificate of its own that we can pin against.
    // One without the other is a chip promising what the next tap cannot deliver,
    // and Paired is the one chip that hides the Pair button.
    if (in.hostPairStatus && in.serverCertStored) { return TrustState::Paired; }
    // Answered and unpaired is a fact about now, whatever is remembered; only a
    // host that did not answer this visit falls back on the memory.
    if (in.probeAnswered) { return TrustState::NotPaired; }
    return in.serverCertStored ? TrustState::Remembered : TrustState::NotPaired;
}

} // namespace dish::moonlight
