// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pure session-FSM decision logic as documentation (no sockets, no Qt):
// the reconcile diff ((lastEpoch,lastBitmap,desired,applied) → action), the
// late-slot converge, the exponential backoff schedule, and the send-counter
// re-push guard. Mirrors the rules in dish-android SatelliteConnection /
// SatelliteConnectionManager that ~93 android tests pin.

#include "core/reducer/Backoff.h"
#include "core/reducer/Reconcile.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace reducer = dish::reducer;
using reducer::AppliedSlot;
using reducer::DesiredSlot;

// ── expectedBitmap ──────────────────────────────────────────────────────────

TEST_CASE("expectedBitmap sets one bit per registered controller index", "[reconcile][bitmap]") {
    REQUIRE(reducer::expectedBitmap({}) == 0);
    REQUIRE(reducer::expectedBitmap({{0, 0}}) == 0x0001);
    REQUIRE(reducer::expectedBitmap({{0, 0}, {2, 0}}) == 0x0005);
    REQUIRE(reducer::expectedBitmap({{15, 0}}) == 0x8000);
    // Out-of-range indices (>15) don't set a bit.
    REQUIRE(reducer::expectedBitmap({{16, 0}}) == 0x0000);
}

// ── reconcileNeeded (the heartbeat-ack drift trigger) ───────────────────────

TEST_CASE("reconcileNeeded: no enriched ack yet -> never", "[reconcile][trigger]") {
    // serverEpoch < 0 means no enriched ack has been seen.
    REQUIRE_FALSE(reducer::reconcileNeeded(-1, -1, 3, 0x0001));
}

TEST_CASE("reconcileNeeded: epoch drift triggers", "[reconcile][trigger]") {
    REQUIRE(reducer::reconcileNeeded(/*srvEpoch=*/4, /*srvBitmap=*/0x0001,
                                     /*lastEpoch=*/3, /*expected=*/0x0001));
}

TEST_CASE("reconcileNeeded: bitmap drift at matching epoch triggers", "[reconcile][trigger]") {
    // The server lost controller 1 (bitmap 0x0001 vs our expected 0x0003).
    REQUIRE(reducer::reconcileNeeded(3, 0x0001, 3, 0x0003));
}

TEST_CASE("reconcileNeeded: epoch+bitmap both match -> no reconcile", "[reconcile][trigger]") {
    REQUIRE_FALSE(reducer::reconcileNeeded(3, 0x0003, 3, 0x0003));
}

TEST_CASE("reconcileNeeded: unknown bitmap (<0) skips the bitmap arm", "[reconcile][trigger]") {
    // serverBitmap < 0 means unknown; only the epoch arm decides.
    REQUIRE_FALSE(reducer::reconcileNeeded(3, -1, 3, 0x0003));
    REQUIRE(reducer::reconcileNeeded(4, -1, 3, 0x0003));
}

// ── appliedMatchesDesired (the GET converge decision) ───────────────────────

TEST_CASE("appliedMatchesDesired: identical sets match", "[reconcile][converge]") {
    std::vector<DesiredSlot> desired = {{0, 0}, {1, 1}};
    std::vector<AppliedSlot> applied = {{0, 0, true}, {1, 1, true}};
    REQUIRE(reducer::appliedMatchesDesired(desired, applied));
}

TEST_CASE("appliedMatchesDesired: a type mismatch forces re-PUT", "[reconcile][converge]") {
    std::vector<DesiredSlot> desired = {{0, 1}};       // want DS4
    std::vector<AppliedSlot> applied = {{0, 0, true}}; // got Xbox
    REQUIRE_FALSE(reducer::appliedMatchesDesired(desired, applied));
}

TEST_CASE("appliedMatchesDesired: an inactive applied slot is unplugged", "[reconcile][converge]") {
    std::vector<DesiredSlot> desired = {{0, 0}};
    std::vector<AppliedSlot> applied = {{0, 0, false}}; // server says inactive
    REQUIRE_FALSE(reducer::appliedMatchesDesired(desired, applied));
}

TEST_CASE("appliedMatchesDesired: a missing desired slot forces re-PUT", "[reconcile][converge]") {
    std::vector<DesiredSlot> desired = {{0, 0}, {1, 0}};
    std::vector<AppliedSlot> applied = {{0, 0, true}}; // server missing slot 1
    REQUIRE_FALSE(reducer::appliedMatchesDesired(desired, applied));
}

TEST_CASE("appliedMatchesDesired: a mouse-grant mismatch forces re-PUT", "[reconcile][converge]") {
    // Even when slots line up, wants≠granted (the grant is only computed at
    // session PUT) forces the converge.
    std::vector<DesiredSlot> desired = {{0, 0}};
    std::vector<AppliedSlot> applied = {{0, 0, true}};
    REQUIRE(reducer::appliedMatchesDesired(desired, applied, /*mouseMatch=*/true));
    REQUIRE_FALSE(reducer::appliedMatchesDesired(desired, applied, /*mouseMatch=*/false));
}

// ── lateSlotConverge (slots that change during the PUT round-trip) ──────────

TEST_CASE("lateSlotConverge: nothing changed -> no follow-ups", "[reconcile][converge]") {
    std::vector<DesiredSlot> sent = {{0, 0}};
    const auto c = reducer::lateSlotConverge(sent, sent);
    REQUIRE(c.resyncs.empty());
    REQUIRE(c.removes.empty());
}

TEST_CASE("lateSlotConverge: a newly-added slot resyncs", "[reconcile][converge]") {
    std::vector<DesiredSlot> sent = {{0, 0}};
    std::vector<DesiredSlot> desired = {{0, 0}, {1, 1}};
    const auto c = reducer::lateSlotConverge(sent, desired);
    REQUIRE(c.resyncs == std::vector<std::uint8_t>{1});
    REQUIRE(c.removes.empty());
}

TEST_CASE("lateSlotConverge: a changed type resyncs", "[reconcile][converge]") {
    std::vector<DesiredSlot> sent = {{0, 0}};
    std::vector<DesiredSlot> desired = {{0, 1}}; // type changed
    const auto c = reducer::lateSlotConverge(sent, desired);
    REQUIRE(c.resyncs == std::vector<std::uint8_t>{0});
    REQUIRE(c.removes.empty());
}

TEST_CASE("lateSlotConverge: a removed slot deletes", "[reconcile][converge]") {
    std::vector<DesiredSlot> sent = {{0, 0}, {1, 0}};
    std::vector<DesiredSlot> desired = {{0, 0}};
    const auto c = reducer::lateSlotConverge(sent, desired);
    REQUIRE(c.resyncs.empty());
    REQUIRE(c.removes == std::vector<std::uint8_t>{1});
}

// ── Backoff schedule (exponential 1s → 60s, contract/android parity) ────────

TEST_CASE("backoffDelayMs is exponential, capped at 60s", "[reconnect][backoff]") {
    REQUIRE(reducer::backoffDelayMs(1) == 1000);  // 1s
    REQUIRE(reducer::backoffDelayMs(2) == 2000);  // 2s
    REQUIRE(reducer::backoffDelayMs(3) == 4000);  // 4s
    REQUIRE(reducer::backoffDelayMs(4) == 8000);  // 8s
    REQUIRE(reducer::backoffDelayMs(5) == 16000); // 16s
    REQUIRE(reducer::backoffDelayMs(6) == 32000); // 32s
    REQUIRE(reducer::backoffDelayMs(7) == 60000); // 1000<<6 = 64000 → capped 60s
    REQUIRE(reducer::backoffDelayMs(8) == 60000); // stays capped
    REQUIRE(reducer::backoffDelayMs(100) == 60000);
}

TEST_CASE("backoffDelayMs treats a non-positive attempt as the first", "[reconnect][backoff]") {
    REQUIRE(reducer::backoffDelayMs(0) == 1000);
    REQUIRE(reducer::backoffDelayMs(-5) == 1000);
}

// ── Send-counter re-push guard (contract §Crypto) ───────────────────────────

TEST_CASE("counterNeedsRepush fires once the send counter crosses 0xF0000000",
          "[reconnect][counter]") {
    REQUIRE_FALSE(reducer::counterNeedsRepush(1));
    REQUIRE_FALSE(reducer::counterNeedsRepush(0xEFFFFFFFu));
    REQUIRE(reducer::counterNeedsRepush(0xF0000000u));
    REQUIRE(reducer::counterNeedsRepush(0xFFFFFFFFu));
}
