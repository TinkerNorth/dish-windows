// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight session lifecycle as a pure, total reducer FSM, in the same
// shape as core/reducer/UsbPathMachine and SatelliteSessionMachine: state+event
// -> next state + effects returned as DATA. The coordinator (Network/
// MoonlightSession) turns world signals into events, runs reduce(), and executes
// the effects against the HTTP / RTSP / ENet layers.
//
// No IO, no Qt. Exhaustively unit-tested one (phase x event) pair at a time.

#pragma once

#include <optional>
#include <vector>

namespace dish::moonlight {

// The phases a Moonlight session moves through. Failed carries a reason.
enum class SessionPhase {
    Idle,              // nothing started
    Pairing,           // PIN pairing in flight (HTTP 5-phase)
    Paired,            // paired, ready to launch an app
    Launching,         // /launch or /resume in flight
    RtspHandshake,     // OPTIONS..PLAY exchange in flight
    ControlConnecting, // ENet connect to the control port
    Streaming,         // control stream live, forwarding input
    Faltering,         // pings missed; recoverable
    Closed,            // graceful teardown finished
    Failed,            // terminal error (see reason)
};

enum class SessionFailure {
    None,
    PairRejected,      // wrong PIN / server refused
    Unreachable,       // HTTP/RTSP transport dead
    LaunchRejected,    // /launch returned an error
    AppAlreadyRunning, // the host already has an app up and offered no resume
    ResumeRejected,    // the host offered a resumable session and then refused it
    RtspFailed,        // RTSP handshake failed
    ControlFailed,     // ENet could not connect
    LinkDropped,       // the control stream closed with no termination from the host
    ServerTerminated,  // host sent TERMINATION / closed
};

struct SessionState {
    SessionPhase phase = SessionPhase::Idle;
    SessionFailure failure = SessionFailure::None;

    bool operator==(const SessionState& o) const {
        return phase == o.phase && failure == o.failure;
    }
    bool operator!=(const SessionState& o) const { return !(*this == o); }
};

// World signals fed into reduce().
enum class SessionEvent {
    StartPairing,
    PairSucceeded,
    PairFailed,
    StartLaunch,
    LaunchSucceeded,
    LaunchFailed,
    // The host answered with an in-body refusal saying an app is already
    // running and named no resumable session. Distinct from LaunchFailed
    // because the only way forward is /cancel, which is the user's call.
    LaunchRefusedBusy,
    // The host said the running session was ours to resume, and then would not
    // hand it back. Distinct from LaunchFailed because the host has a session and
    // the only way forward is to close it.
    ResumeRefused,
    RtspSucceeded,
    RtspFailed,
    ControlConnected,
    ControlConnectFailed,
    PingsMissed,      // heartbeat window elapsed with no traffic
    PingsRecovered,   // traffic resumed
    ServerTerminated, // host sent TERMINATION, or the app closed
    // The control stream went away without the host saying why. Recoverable: the
    // host will usually let us resume, which a termination will not.
    ControlDropped,
    Unreachable, // an HTTP/RTSP call could not reach the host
    UserQuit,    // local teardown request
};

// Effects the coordinator performs. Returned as data, executed at the edge.
enum class SessionEffect {
    BeginPairing,   // run the 5-phase HTTP pairing
    BeginLaunch,    // POST /launch (or /resume)
    BeginRtsp,      // run the RTSP handshake
    ConnectControl, // ENet-connect the control channel
    SendArrival,    // send CONTROLLER_ARRIVAL for each pad
    StartPinging,   // begin the PERIODIC_PING keepalive
    StopPinging,    // pause pinging while faltering
    // LOCAL ONLY: stop pinging, close the media sockets, TERMINATION + ENet
    // disconnect. It is deliberately separate from the one below, because
    // bringing our own end down and telling the host to close an app are two
    // different acts with two different consequences for the person at the host.
    Teardown,
    // GET /cancel: the host closes whatever it is running for us. Emitted only
    // where the host actually has an app of ours to close, and NEVER on a link
    // that merely dropped: a drop is as likely to be a blip as an ending, and
    // closing somebody's game out from under them is worse than the tidying is
    // worth.
    CancelOnHost,
    NotifyFailure, // surface the terminal reason
};

// Whether the host is running an app because THIS session asked it to. A launch
// still in flight has been asked and not answered, so it is not on this list:
// speculatively cancelling one would close an app we do not know exists, and the
// answer arrives soon enough to be acted on (see LaunchSucceeded below).
inline bool hostHoldsOurApp(SessionPhase phase) {
    switch (phase) {
    case SessionPhase::RtspHandshake:
    case SessionPhase::ControlConnecting:
    case SessionPhase::Streaming:
    case SessionPhase::Faltering:
        return true;
    case SessionPhase::Idle:
    case SessionPhase::Pairing:
    case SessionPhase::Paired:
    case SessionPhase::Launching:
    case SessionPhase::Closed:
    case SessionPhase::Failed:
        return false;
    }
    return false;
}

// Whether this session has an attempt of its own underway or up, which is what
// separates closing OUR app from closing one another device left running: the
// second has nothing local to tear down and the bare /cancel is the whole act.
inline bool sessionAttemptInFlight(SessionPhase phase) {
    return phase == SessionPhase::Launching || hostHoldsOurApp(phase);
}

struct SessionReduction {
    std::optional<SessionState> next; // nullopt: no state change
    std::vector<SessionEffect> effects;
};

// Pure, total. Defined for every (phase x event) pair; an event that does not
// apply in the current phase is a no-op (nullopt next, no effects).
inline SessionReduction reduceSession(const SessionState& s, SessionEvent e) {
    auto to = [](SessionPhase p, SessionFailure f = SessionFailure::None) {
        return SessionState{p, f};
    };

    switch (e) {
    case SessionEvent::StartPairing:
        if (s.phase == SessionPhase::Idle || s.phase == SessionPhase::Failed ||
            s.phase == SessionPhase::Closed) {
            return {to(SessionPhase::Pairing), {SessionEffect::BeginPairing}};
        }
        return {};
    case SessionEvent::PairSucceeded:
        if (s.phase == SessionPhase::Pairing) { return {to(SessionPhase::Paired), {}}; }
        return {};
    case SessionEvent::PairFailed:
        if (s.phase == SessionPhase::Pairing) {
            return {to(SessionPhase::Failed, SessionFailure::PairRejected),
                    {SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::StartLaunch:
        // A remembered host is already paired, so a launch does not have to be
        // preceded by pairing in this run; and a launch that failed or a session
        // that closed can be retried without one either.
        if (s.phase == SessionPhase::Idle || s.phase == SessionPhase::Paired ||
            s.phase == SessionPhase::Closed || s.phase == SessionPhase::Failed) {
            return {to(SessionPhase::Launching), {SessionEffect::BeginLaunch}};
        }
        return {};
    case SessionEvent::LaunchSucceeded:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::RtspHandshake), {SessionEffect::BeginRtsp}};
        }
        // THE LAUNCH WE WALKED AWAY FROM CAME GOOD ANYWAY. The last pad left
        // while the reply was still out, so nothing here wants the session any
        // more, but the host has now started an app on our account and it is the
        // app that refuses every later /launch. Taking it back down is the whole
        // of "the client quits only what it started": no state moves, because
        // the session is already closed.
        if (s.phase == SessionPhase::Closed) {
            return {std::nullopt, {SessionEffect::CancelOnHost}};
        }
        return {};
    case SessionEvent::LaunchFailed:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::Failed, SessionFailure::LaunchRejected),
                    {SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::LaunchRefusedBusy:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::Failed, SessionFailure::AppAlreadyRunning),
                    {SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::ResumeRefused:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::Failed, SessionFailure::ResumeRejected),
                    {SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::RtspSucceeded:
        if (s.phase == SessionPhase::RtspHandshake) {
            return {to(SessionPhase::ControlConnecting), {SessionEffect::ConnectControl}};
        }
        return {};
    case SessionEvent::RtspFailed:
        if (s.phase == SessionPhase::RtspHandshake) {
            // THE LAUNCH ALREADY SUCCEEDED. We are here because the host started
            // an app for us and the stream then would not come up, so the app is
            // ours to take back down: left running it is the very thing that
            // refuses the next /launch, and the copy this state renders promises
            // the user we closed it.
            return {to(SessionPhase::Failed, SessionFailure::RtspFailed),
                    {SessionEffect::Teardown, SessionEffect::CancelOnHost,
                     SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::ControlConnected:
        if (s.phase == SessionPhase::ControlConnecting) {
            return {to(SessionPhase::Streaming),
                    {SessionEffect::SendArrival, SessionEffect::StartPinging}};
        }
        return {};
    case SessionEvent::ControlConnectFailed:
        if (s.phase == SessionPhase::ControlConnecting) {
            // Same as RtspFailed above, one step later: the app is running on the
            // host on our account and nothing is going to ride it.
            return {to(SessionPhase::Failed, SessionFailure::ControlFailed),
                    {SessionEffect::Teardown, SessionEffect::CancelOnHost,
                     SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::PingsMissed:
        if (s.phase == SessionPhase::Streaming) { return {to(SessionPhase::Faltering), {}}; }
        return {};
    case SessionEvent::PingsRecovered:
        if (s.phase == SessionPhase::Faltering) { return {to(SessionPhase::Streaming), {}}; }
        return {};
    case SessionEvent::ServerTerminated:
        if (s.phase == SessionPhase::Streaming || s.phase == SessionPhase::Faltering ||
            s.phase == SessionPhase::ControlConnecting) {
            return {to(SessionPhase::Failed, SessionFailure::ServerTerminated),
                    {SessionEffect::Teardown, SessionEffect::CancelOnHost,
                     SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::ControlDropped:
        if (s.phase == SessionPhase::Streaming || s.phase == SessionPhase::Faltering) {
            // OUR END COMES DOWN AND THE HOST IS TOLD NOTHING. A control stream
            // that closed without a termination is as likely to be a Wi-Fi blip
            // as an ending, and the host will usually hand the session back on a
            // /resume; closing the app would take somebody's game with it. This
            // is the one teardown in the machine with no /cancel beside it, and
            // it is why a drop and an ending are two states and not one.
            return {to(SessionPhase::Failed, SessionFailure::LinkDropped),
                    {SessionEffect::Teardown, SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::Unreachable:
        // Any in-flight HTTP/RTSP phase can hit a dead transport.
        if (s.phase == SessionPhase::Pairing || s.phase == SessionPhase::Launching ||
            s.phase == SessionPhase::RtspHandshake) {
            return {to(SessionPhase::Failed, SessionFailure::Unreachable),
                    {SessionEffect::NotifyFailure}};
        }
        return {};
    case SessionEvent::UserQuit:
        if (s.phase == SessionPhase::Idle || s.phase == SessionPhase::Closed ||
            s.phase == SessionPhase::Failed) {
            return {}; // already down
        }
        // The /cancel goes only where the host has an app of ours to close. A
        // pairing, or a host merely paired with, has been asked for nothing, and
        // a /cancel there is a mutual-TLS round trip that closes whatever the
        // person at that machine happened to be running.
        if (hostHoldsOurApp(s.phase)) {
            return {to(SessionPhase::Closed),
                    {SessionEffect::Teardown, SessionEffect::CancelOnHost}};
        }
        return {to(SessionPhase::Closed), {SessionEffect::Teardown}};
    }
    return {};
}

} // namespace dish::moonlight
