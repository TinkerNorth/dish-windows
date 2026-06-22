// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pins the wake DERIVE half: WakeStateComposer folds (streamingSlotCount,
// shouldKeepScreenOn) into a WakeState whose shouldInhibit is true iff either is
// positive, with the Combiner's distinct-until-changed giving the 0<->positive
// no-thrash guarantee. Replicates dish-android composer/WakeStateComposerTest
// (3 cases: streamingSlotCount + shouldKeepScreenOn > 0).

#include "composer/WakeStateComposer.h"

#include "ComposerProbe.h"

#include <catch2/catch_test_macros.hpp>

using dish::arch::Observable;
using dish::composer::deriveWakeState;
using dish::composer::WakeState;
using dish::composer::WakeStateComposer;
using dish::test::ComposerProbe;

// ── Pure derivation ───────────────────────────────────────────────────────────

TEST_CASE("deriveWakeState: zero count and no override does not inhibit", "[wake]") {
    REQUIRE_FALSE(deriveWakeState(0, 0).shouldInhibit);
}

TEST_CASE("deriveWakeState: a positive streaming count inhibits", "[wake]") {
    REQUIRE(deriveWakeState(1, 0).shouldInhibit);
    REQUIRE(deriveWakeState(3, 0).shouldInhibit);
}

TEST_CASE("deriveWakeState: a keep-screen-on override inhibits even at zero count", "[wake]") {
    // The override alone holds the screen awake (e.g. a settings toggle).
    REQUIRE(deriveWakeState(0, 1).shouldInhibit);
}

// ── Composer over Observables ─────────────────────────────────────────────────

TEST_CASE("WakeStateComposer: eager initial reflects the current inputs", "[wake]") {
    Observable<int> count(0);
    Observable<int> keepOn(0);
    WakeStateComposer composer(count, keepOn);
    REQUIRE_FALSE(composer.state().value().shouldInhibit);
}

TEST_CASE("WakeStateComposer: recomputes on a streaming-count change", "[wake]") {
    Observable<int> count(0);
    Observable<int> keepOn(0);
    WakeStateComposer composer(count, keepOn);
    ComposerProbe<WakeState> probe(composer.state());

    count.set(2);
    REQUIRE(composer.state().value().shouldInhibit);
    REQUIRE(composer.state().value().streamingSlotCount == 2);

    count.set(0);
    REQUIRE_FALSE(composer.state().value().shouldInhibit);
    // eager initial (0) + ->2 + ->0
    REQUIRE(probe.count() == 3);
}

TEST_CASE("WakeStateComposer: distinct-until-changed suppresses same-bool re-emits", "[wake]") {
    Observable<int> count(0);
    Observable<int> keepOn(0);
    WakeStateComposer composer(count, keepOn);

    count.set(1);
    ComposerProbe<WakeState> probe(composer.state()); // starts after going positive
    // 1 -> 2 -> 3: shouldInhibit stays true, but streamingSlotCount changes, so
    // the WakeState value differs and DOES re-emit (carried for diagnostics).
    count.set(2);
    count.set(3);
    REQUIRE(probe.count() == 3); // initial(1) + 2 + 3

    // Now keep the count the same: no emit.
    const auto before = probe.count();
    count.set(3);
    REQUIRE(probe.count() == before);
}
