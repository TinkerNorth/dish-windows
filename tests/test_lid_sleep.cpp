// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What Dish remembers about the lid across a sleep, and when that earns the one
// toast after resume. Windows clears every power request when the lid starts
// sleep (Microsoft's PowerSetRequest remarks), so the toast is the only honest
// thing left to do; these pin that it fires once, only when streaming was
// paused, and only when the lid was closed for that sleep.

#include "core/reducer/LidSleep.h"
#include "core/reducer/SleepCycle.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string_view>

using dish::reducer::LidNotice;
using dish::reducer::LidPosition;
using dish::reducer::lidPositionFromSwitchState;
using dish::reducer::lidPositionName;
using dish::reducer::LidSleepReduction;
using dish::reducer::LidSleepState;
using dish::reducer::reduceLidChange;
using dish::reducer::reduceLidOnSleepEffect;
using dish::reducer::SleepEffect;

namespace {

constexpr bool kStreaming = true;
constexpr bool kIdle = false;

// The state after the lid reports `lid` and then a sleep begins.
LidSleepState asleepWith(LidPosition lid, bool streaming) {
    const LidSleepState withLid = reduceLidChange(LidSleepState{}, lid).next;
    return reduceLidOnSleepEffect(withLid, SleepEffect::TearDown, streaming).next;
}

} // namespace

TEST_CASE("lid switch: Windows' DWORD 0 is closed and 1 is open", "[lid]") {
    // GUID_LIDSWITCH_STATE_CHANGE's documented payload.
    REQUIRE(lidPositionFromSwitchState(0) == LidPosition::Closed);
    REQUIRE(lidPositionFromSwitchState(1) == LidPosition::Open);
}

TEST_CASE("lid switch: any other value is unknown, never open", "[lid]") {
    REQUIRE(lidPositionFromSwitchState(2) == LidPosition::Unknown);
    REQUIRE(lidPositionFromSwitchState(UINT32_MAX) == LidPosition::Unknown);
}

TEST_CASE("lid position names are what the log prints", "[lid]") {
    REQUIRE(lidPositionName(LidPosition::Unknown) == std::string_view("unknown"));
    REQUIRE(lidPositionName(LidPosition::Open) == std::string_view("open"));
    REQUIRE(lidPositionName(LidPosition::Closed) == std::string_view("closed"));
}

TEST_CASE("lid state: starts unknown, awake, with nothing to announce", "[lid]") {
    const LidSleepState s;
    REQUIRE(s.lid == LidPosition::Unknown);
    REQUIRE_FALSE(s.asleep);
    REQUIRE_FALSE(s.streamingWhenSleepBegan);
    REQUIRE_FALSE(s.lidClosedAcrossSleep);
}

TEST_CASE("lid state: a lid change while awake only records the position", "[lid]") {
    const LidSleepReduction closed = reduceLidChange(LidSleepState{}, LidPosition::Closed);
    REQUIRE(closed.next.lid == LidPosition::Closed);
    REQUIRE_FALSE(closed.next.lidClosedAcrossSleep);
    REQUIRE(closed.notice == LidNotice::None);

    const LidSleepReduction opened = reduceLidChange(closed.next, LidPosition::Open);
    REQUIRE(opened.next.lid == LidPosition::Open);
    REQUIRE(opened.notice == LidNotice::None);
}

TEST_CASE("lid state: a sleep that begins with the lid closed remembers it", "[lid]") {
    const LidSleepState s = asleepWith(LidPosition::Closed, kStreaming);
    REQUIRE(s.asleep);
    REQUIRE(s.streamingWhenSleepBegan);
    REQUIRE(s.lidClosedAcrossSleep);
}

TEST_CASE("lid state: a sleep that begins with the lid open does not", "[lid]") {
    REQUIRE_FALSE(asleepWith(LidPosition::Open, kStreaming).lidClosedAcrossSleep);
    REQUIRE_FALSE(asleepWith(LidPosition::Unknown, kStreaming).lidClosedAcrossSleep);
}

TEST_CASE("lid state: a close reported after the suspend edge still counts", "[lid]") {
    // The lid notification and PBT_APMSUSPEND are separate messages, and the
    // order they arrive in is not documented.
    const LidSleepState asleep = asleepWith(LidPosition::Open, kStreaming);
    const LidSleepReduction closed = reduceLidChange(asleep, LidPosition::Closed);
    REQUIRE(closed.next.lidClosedAcrossSleep);
    REQUIRE(closed.notice == LidNotice::None);
}

TEST_CASE("lid state: opening the lid before the resume edge keeps the memory", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Closed, kStreaming);
    const LidSleepState opened = reduceLidChange(asleep, LidPosition::Open).next;
    REQUIRE(opened.lidClosedAcrossSleep);
    const LidSleepReduction resumed = reduceLidOnSleepEffect(opened, SleepEffect::Reconnect, kIdle);
    REQUIRE(resumed.notice == LidNotice::PausedWhileLidClosed);
}

TEST_CASE("lid toast: a resume after a lid-closed sleep that paused streaming says so", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Closed, kStreaming);
    const LidSleepReduction resumed = reduceLidOnSleepEffect(asleep, SleepEffect::Reconnect, kIdle);
    REQUIRE(resumed.notice == LidNotice::PausedWhileLidClosed);
    REQUIRE_FALSE(resumed.next.asleep);
    REQUIRE_FALSE(resumed.next.lidClosedAcrossSleep);
    REQUIRE_FALSE(resumed.next.streamingWhenSleepBegan);
    REQUIRE(resumed.next.lid == LidPosition::Closed);
}

TEST_CASE("lid toast: nothing streaming when the lid closed means nothing to say", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Closed, kIdle);
    const LidSleepReduction resumed =
        reduceLidOnSleepEffect(asleep, SleepEffect::Reconnect, kStreaming);
    REQUIRE(resumed.notice == LidNotice::None);
}

TEST_CASE("lid toast: a sleep with the lid open is not blamed on the lid", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Open, kStreaming);
    const LidSleepReduction resumed = reduceLidOnSleepEffect(asleep, SleepEffect::Reconnect, kIdle);
    REQUIRE(resumed.notice == LidNotice::None);
}

TEST_CASE("lid toast: fires once, never again on the next resume", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Closed, kStreaming);
    const LidSleepState resumed =
        reduceLidOnSleepEffect(asleep, SleepEffect::Reconnect, kIdle).next;
    const LidSleepReduction again = reduceLidOnSleepEffect(resumed, SleepEffect::Reconnect, kIdle);
    REQUIRE(again.notice == LidNotice::None);
}

TEST_CASE("lid toast: a resume with no sleep before it says nothing", "[lid]") {
    const LidSleepState closed = reduceLidChange(LidSleepState{}, LidPosition::Closed).next;
    const LidSleepReduction resumed = reduceLidOnSleepEffect(closed, SleepEffect::Reconnect, kIdle);
    REQUIRE(resumed.notice == LidNotice::None);
}

TEST_CASE("lid toast: an inert sleep edge changes nothing", "[lid]") {
    const LidSleepState asleep = asleepWith(LidPosition::Closed, kStreaming);
    const LidSleepReduction inert = reduceLidOnSleepEffect(asleep, SleepEffect::None, kIdle);
    REQUIRE(inert.next == asleep);
    REQUIRE(inert.notice == LidNotice::None);
}

TEST_CASE("lid state: equality covers every field", "[lid]") {
    const LidSleepState base;
    LidSleepState lid = base;
    lid.lid = LidPosition::Open;
    LidSleepState asleep = base;
    asleep.asleep = true;
    LidSleepState streaming = base;
    streaming.streamingWhenSleepBegan = true;
    LidSleepState closed = base;
    closed.lidClosedAcrossSleep = true;
    REQUIRE(base == LidSleepState{});
    REQUIRE(base != lid);
    REQUIRE(base != asleep);
    REQUIRE(base != streaming);
    REQUIRE(base != closed);
}
