// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight session FSM: one assertion per (phase x event) transition of
// interest, plus the full happy path and the terminal arms.

#include "core/moonlight/MoonlightSessionMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace dish::moonlight;

namespace {
bool has(const std::vector<SessionEffect>& v, SessionEffect e) {
    return std::find(v.begin(), v.end(), e) != v.end();
}
SessionState st(SessionPhase p, SessionFailure f = SessionFailure::None) { return {p, f}; }
} // namespace

TEST_CASE("Happy path Idle -> Streaming", "[moonlight][session]") {
    SessionState s = st(SessionPhase::Idle);

    auto r = reduceSession(s, SessionEvent::StartPairing);
    REQUIRE(r.next.has_value());
    REQUIRE(r.next->phase == SessionPhase::Pairing);
    REQUIRE(has(r.effects, SessionEffect::BeginPairing));
    s = *r.next;

    r = reduceSession(s, SessionEvent::PairSucceeded);
    REQUIRE(r.next->phase == SessionPhase::Paired);
    s = *r.next;

    r = reduceSession(s, SessionEvent::StartLaunch);
    REQUIRE(r.next->phase == SessionPhase::Launching);
    REQUIRE(has(r.effects, SessionEffect::BeginLaunch));
    s = *r.next;

    r = reduceSession(s, SessionEvent::LaunchSucceeded);
    REQUIRE(r.next->phase == SessionPhase::RtspHandshake);
    REQUIRE(has(r.effects, SessionEffect::BeginRtsp));
    s = *r.next;

    r = reduceSession(s, SessionEvent::RtspSucceeded);
    REQUIRE(r.next->phase == SessionPhase::ControlConnecting);
    REQUIRE(has(r.effects, SessionEffect::ConnectControl));
    s = *r.next;

    r = reduceSession(s, SessionEvent::ControlConnected);
    REQUIRE(r.next->phase == SessionPhase::Streaming);
    REQUIRE(has(r.effects, SessionEffect::SendArrival));
    REQUIRE(has(r.effects, SessionEffect::StartPinging));
}

TEST_CASE("Faltering toggles on missed and recovered pings", "[moonlight][session]") {
    SessionState s = st(SessionPhase::Streaming);
    auto r = reduceSession(s, SessionEvent::PingsMissed);
    REQUIRE(r.next->phase == SessionPhase::Faltering);
    s = *r.next;
    r = reduceSession(s, SessionEvent::PingsRecovered);
    REQUIRE(r.next->phase == SessionPhase::Streaming);
}

TEST_CASE("Terminal failures carry a reason and notify", "[moonlight][session]") {
    {
        auto r = reduceSession(st(SessionPhase::Pairing), SessionEvent::PairFailed);
        REQUIRE(r.next->phase == SessionPhase::Failed);
        REQUIRE(r.next->failure == SessionFailure::PairRejected);
        REQUIRE(has(r.effects, SessionEffect::NotifyFailure));
    }
    {
        auto r = reduceSession(st(SessionPhase::Launching), SessionEvent::LaunchFailed);
        REQUIRE(r.next->failure == SessionFailure::LaunchRejected);
    }
    {
        auto r = reduceSession(st(SessionPhase::RtspHandshake), SessionEvent::RtspFailed);
        REQUIRE(r.next->failure == SessionFailure::RtspFailed);
    }
    {
        auto r =
            reduceSession(st(SessionPhase::ControlConnecting), SessionEvent::ControlConnectFailed);
        REQUIRE(r.next->failure == SessionFailure::ControlFailed);
    }
    {
        auto r = reduceSession(st(SessionPhase::Pairing), SessionEvent::Unreachable);
        REQUIRE(r.next->failure == SessionFailure::Unreachable);
    }
}

TEST_CASE("ServerTerminated tears down from any live phase", "[moonlight][session]") {
    for (auto phase :
         {SessionPhase::Streaming, SessionPhase::Faltering, SessionPhase::ControlConnecting}) {
        auto r = reduceSession(st(phase), SessionEvent::ServerTerminated);
        REQUIRE(r.next.has_value());
        REQUIRE(r.next->phase == SessionPhase::Failed);
        REQUIRE(r.next->failure == SessionFailure::ServerTerminated);
        REQUIRE(has(r.effects, SessionEffect::Teardown));
    }
    // Not applicable before there is a stream.
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Paired), SessionEvent::ServerTerminated).next.has_value());
}

TEST_CASE("UserQuit tears down a live session and is a no-op when down", "[moonlight][session]") {
    auto r = reduceSession(st(SessionPhase::Streaming), SessionEvent::UserQuit);
    REQUIRE(r.next->phase == SessionPhase::Closed);
    REQUIRE(has(r.effects, SessionEffect::Teardown));

    for (auto p : {SessionPhase::Idle, SessionPhase::Closed, SessionPhase::Failed}) {
        REQUIRE_FALSE(reduceSession(st(p), SessionEvent::UserQuit).next.has_value());
    }
}

TEST_CASE("Out-of-order events are inert no-ops", "[moonlight][session]") {
    // PairSucceeded outside Pairing.
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Idle), SessionEvent::PairSucceeded).next.has_value());
    // ControlConnected before connecting.
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Paired), SessionEvent::ControlConnected).next.has_value());
    // PingsRecovered while already streaming.
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Streaming), SessionEvent::PingsRecovered).next.has_value());
    // StartPairing can restart from a terminal state.
    REQUIRE(reduceSession(st(SessionPhase::Failed), SessionEvent::StartPairing).next.has_value());
}
