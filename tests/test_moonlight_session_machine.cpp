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

TEST_CASE("A launch starts from a remembered host and from a terminal state",
          "[moonlight][session]") {
    // A remembered host is already paired, so the first thing a fresh session
    // object does can be a launch; and a failed launch or a closed session can
    // be retried without pairing again. Without this the connect button on a
    // remembered host reduced to nothing at all.
    for (auto phase :
         {SessionPhase::Idle, SessionPhase::Paired, SessionPhase::Closed, SessionPhase::Failed}) {
        auto r = reduceSession(st(phase), SessionEvent::StartLaunch);
        REQUIRE(r.next.has_value());
        REQUIRE(r.next->phase == SessionPhase::Launching);
        REQUIRE(r.next->failure == SessionFailure::None);
        REQUIRE(has(r.effects, SessionEffect::BeginLaunch));
    }
    // But not on top of a launch already in flight, or a live stream.
    for (auto phase :
         {SessionPhase::Launching, SessionPhase::RtspHandshake, SessionPhase::ControlConnecting,
          SessionPhase::Streaming, SessionPhase::Pairing}) {
        REQUIRE_FALSE(reduceSession(st(phase), SessionEvent::StartLaunch).next.has_value());
    }
}

TEST_CASE("A busy host is a failure of its own, not a generic launch rejection",
          "[moonlight][session]") {
    // The host answered 200 with an in-body status_code saying an app is already
    // running and named no resumable session. The only way forward is /cancel,
    // which is the user's call, so it does not share LaunchRejected's reason.
    auto r = reduceSession(st(SessionPhase::Launching), SessionEvent::LaunchRefusedBusy);
    REQUIRE(r.next.has_value());
    REQUIRE(r.next->phase == SessionPhase::Failed);
    REQUIRE(r.next->failure == SessionFailure::AppAlreadyRunning);
    REQUIRE(has(r.effects, SessionEffect::NotifyFailure));
    // No teardown: there is nothing of ours to tear down, and /cancel would
    // stop an app the user may not want stopped.
    REQUIRE_FALSE(has(r.effects, SessionEffect::Teardown));

    // Outside a launch it is inert.
    for (auto phase : {SessionPhase::Idle, SessionPhase::Paired, SessionPhase::Streaming}) {
        REQUIRE_FALSE(reduceSession(st(phase), SessionEvent::LaunchRefusedBusy).next.has_value());
    }
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
        // The host ended it: nothing of ours is running to quit, and a /cancel
        // now would close whatever the person at that machine started next.
        REQUIRE_FALSE(has(r.effects, SessionEffect::CancelOnHost));
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

TEST_CASE("A resume the host offered and then refused is its own failure", "[moonlight][session]") {
    // The only way forward is closing the app on the host, which is a different
    // sentence and a different button from a plain launch refusal.
    auto r = reduceSession(st(SessionPhase::Launching), SessionEvent::ResumeRefused);
    REQUIRE(r.next.has_value());
    REQUIRE(r.next->phase == SessionPhase::Failed);
    REQUIRE(r.next->failure == SessionFailure::ResumeRejected);
    REQUIRE(has(r.effects, SessionEffect::NotifyFailure));

    // It applies only where a resume can be in flight.
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Streaming), SessionEvent::ResumeRefused).next.has_value());
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Idle), SessionEvent::ResumeRefused).next.has_value());
}

TEST_CASE("A dropped link and a host that ended the session are not the same thing",
          "[moonlight][session]") {
    // A drop is recoverable and the host will usually let us resume; a
    // termination is the host saying no.
    auto dropped = reduceSession(st(SessionPhase::Streaming), SessionEvent::ControlDropped);
    REQUIRE(dropped.next.has_value());
    REQUIRE(dropped.next->phase == SessionPhase::Failed);
    REQUIRE(dropped.next->failure == SessionFailure::LinkDropped);
    REQUIRE(has(dropped.effects, SessionEffect::Teardown));
    REQUIRE(has(dropped.effects, SessionEffect::NotifyFailure));

    auto faltering = reduceSession(st(SessionPhase::Faltering), SessionEvent::ControlDropped);
    REQUIRE(faltering.next.has_value());
    REQUIRE(faltering.next->failure == SessionFailure::LinkDropped);

    auto ended = reduceSession(st(SessionPhase::Streaming), SessionEvent::ServerTerminated);
    REQUIRE(ended.next.has_value());
    REQUIRE(ended.next->failure == SessionFailure::ServerTerminated);

    // A stream that was never up cannot drop.
    REQUIRE_FALSE(reduceSession(st(SessionPhase::ControlConnecting), SessionEvent::ControlDropped)
                      .next.has_value());
    REQUIRE_FALSE(
        reduceSession(st(SessionPhase::Idle), SessionEvent::ControlDropped).next.has_value());
}

TEST_CASE("A dropped session can be launched again without pairing again", "[moonlight][session]") {
    // The binding survives the drop, so the next use retries rather than asking
    // the user to start over.
    const SessionState dropped{SessionPhase::Failed, SessionFailure::LinkDropped};
    auto r = reduceSession(dropped, SessionEvent::StartLaunch);
    REQUIRE(r.next.has_value());
    REQUIRE(r.next->phase == SessionPhase::Launching);
    REQUIRE(has(r.effects, SessionEffect::BeginLaunch));
}
