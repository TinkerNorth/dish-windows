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
    PairRejected,     // wrong PIN / server refused
    Unreachable,      // HTTP/RTSP transport dead
    LaunchRejected,   // /launch returned an error
    RtspFailed,       // RTSP handshake failed
    ControlFailed,    // ENet could not connect
    ServerTerminated, // host sent TERMINATION / closed
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
    RtspSucceeded,
    RtspFailed,
    ControlConnected,
    ControlConnectFailed,
    PingsMissed,      // heartbeat window elapsed with no traffic
    PingsRecovered,   // traffic resumed
    ServerTerminated, // host sent TERMINATION or disconnected
    Unreachable,      // an HTTP/RTSP call could not reach the host
    UserQuit,         // local teardown request
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
    Teardown,       // TERMINATION + ENet disconnect + /cancel
    NotifyFailure,  // surface the terminal reason
};

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
        if (s.phase == SessionPhase::Paired) {
            return {to(SessionPhase::Launching), {SessionEffect::BeginLaunch}};
        }
        return {};
    case SessionEvent::LaunchSucceeded:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::RtspHandshake), {SessionEffect::BeginRtsp}};
        }
        return {};
    case SessionEvent::LaunchFailed:
        if (s.phase == SessionPhase::Launching) {
            return {to(SessionPhase::Failed, SessionFailure::LaunchRejected),
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
            return {to(SessionPhase::Failed, SessionFailure::RtspFailed),
                    {SessionEffect::NotifyFailure}};
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
            return {to(SessionPhase::Failed, SessionFailure::ControlFailed),
                    {SessionEffect::NotifyFailure}};
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
        return {to(SessionPhase::Closed), {SessionEffect::Teardown}};
    }
    return {};
}

} // namespace dish::moonlight
