// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The resend pacing gate (edge-burst + keepalive) with a fake clock. Heals a
// lost final edge frame by re-sending a changed state for the burst window,
// then falls back to one keepalive per interval. Replicates dish-android
// ui/common/ResendPacerTest (4), driven by an injected nanoTime instead of a
// coroutine-test clock.

#include "core/reducer/ResendPacer.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using dish::reducer::ResendPacer;

namespace {

// A scriptable fake clock + tick helper, mirroring the android test's `tick`:
// advance the clock by one scheduler interval, then ask the gate.
struct Harness {
    std::int64_t nowNs = 1'000'000'000;                 // non-zero so "never sent" != "sent at t=0"
    static constexpr std::int64_t kTickNs = 50'000'000; // the overlays' scheduler interval
    ResendPacer pacer{[this] { return nowNs; }};

    bool tick(bool changed) {
        nowNs += kTickNs;
        return pacer.resendDue(changed);
    }
};

} // namespace

TEST_CASE("ResendPacer: a change sends immediately plus the rest of the edge burst, then quiets",
          "[pacer]") {
    Harness h;
    REQUIRE(h.tick(/*changed=*/true));
    // kEdgeBurstResends sends total: the change tick + two unchanged ticks.
    REQUIRE(h.tick(false));
    REQUIRE(h.tick(false));
    REQUIRE_FALSE(h.tick(false));
    REQUIRE_FALSE(h.tick(false));
}

TEST_CASE("ResendPacer: steady state sends exactly one keepalive per interval", "[pacer]") {
    Harness h;
    REQUIRE(h.tick(true));
    for (int i = 0; i < ResendPacer::kEdgeBurstResends - 1; ++i) { REQUIRE(h.tick(false)); }

    int sends = 0;
    // Two keepalive intervals of unchanged ticks -> exactly two sends.
    const int ticks = static_cast<int>(2 * ResendPacer::kKeepaliveIntervalNs / Harness::kTickNs);
    for (int i = 0; i < ticks; ++i) {
        if (h.tick(false)) { ++sends; }
    }
    REQUIRE(sends == 2);
}

TEST_CASE("ResendPacer: a change mid-burst restarts the burst from that tick", "[pacer]") {
    Harness h;
    REQUIRE(h.tick(true));
    REQUIRE(h.tick(false)); // burst tick 2 of 3
    REQUIRE(h.tick(true));  // new change, burst restarts
    REQUIRE(h.tick(false));
    REQUIRE(h.tick(false));
    REQUIRE_FALSE(h.tick(false));
}

TEST_CASE("ResendPacer: keepalive clock restarts from the last burst send", "[pacer]") {
    Harness h;
    REQUIRE(h.tick(true));
    for (int i = 0; i < ResendPacer::kEdgeBurstResends - 1; ++i) { REQUIRE(h.tick(false)); }
    const std::int64_t lastBurstSendNs = h.nowNs;

    // Just short of one keepalive interval after the LAST burst send: quiet.
    h.nowNs = lastBurstSendNs + ResendPacer::kKeepaliveIntervalNs - 2 * Harness::kTickNs;
    REQUIRE_FALSE(h.tick(false));
    // The next tick crosses the interval: one keepalive send.
    REQUIRE(h.tick(false));
}
