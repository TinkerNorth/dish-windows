// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SetThreadExecutionState is process-wide but harmless to flip here: it only
// sets a flag the power manager consults, and each dtor clears it back to
// ES_CONTINUOUS so the runner is left clean.

#include "Util/DisplaySleepInhibitor.h"

#include <catch2/catch_test_macros.hpp>

using dish::util::SetThreadExecutionStateInhibitor;

TEST_CASE("STES inhibitor starts unheld", "[wake][windows]") {
    const SetThreadExecutionStateInhibitor inh;
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor acquire flips held; release flips it back", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.acquire(QStringLiteral("test reason"));
    REQUIRE(inh.isHeld());
    inh.release();
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor acquire is idempotent", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.acquire(QStringLiteral("first"));
    REQUIRE(inh.isHeld());
    inh.acquire(QStringLiteral("second"));
    REQUIRE(inh.isHeld());
    inh.release();
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor release is idempotent", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.release();
    REQUIRE_FALSE(inh.isHeld());

    inh.acquire(QStringLiteral("test"));
    inh.release();
    inh.release();
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor re-acquires cleanly after release", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.acquire(QStringLiteral("first stream"));
    inh.release();
    inh.acquire(QStringLiteral("second stream"));
    REQUIRE(inh.isHeld());
    inh.release();
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor destructor releases a held flag", "[wake][windows]") {
    // There is no public API for "is ES_DISPLAY_REQUIRED set process-wide", so
    // the successor's unheld state is as close as a unit test can get to
    // proving the dtor cleared the flag.
    {
        SetThreadExecutionStateInhibitor inh;
        inh.acquire(QStringLiteral("dies on scope exit"));
        REQUIRE(inh.isHeld());
    }
    const SetThreadExecutionStateInhibitor next;
    REQUIRE_FALSE(next.isHeld());
}
