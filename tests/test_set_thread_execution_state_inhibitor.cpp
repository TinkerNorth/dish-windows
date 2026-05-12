// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pins the production DisplaySleepInhibitor implementation (the one
// backed by Win32 `SetThreadExecutionState`). `test_screen_wake_controller.cpp`
// already covers the abstract DisplaySleepInhibitor contract via a fake;
// this file exercises the concrete impl too so the lifecycle isn't a
// "checked at runtime only" surface.
//
// SetThreadExecutionState is process-wide and harmless to flip in a test
// — it just sets a flag the power manager consults. Each test case
// constructs a fresh inhibitor; the dtor clears the flag back to
// ES_CONTINUOUS so we leave the runner state clean.

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
    // Second acquire while already held is a no-op — the held_ flag stays
    // true, but we don't re-call SetThreadExecutionState (which would be
    // harmless but wasteful).
    inh.acquire(QStringLiteral("second"));
    REQUIRE(inh.isHeld());
    inh.release();
    REQUIRE_FALSE(inh.isHeld());
}

TEST_CASE("STES inhibitor release is idempotent", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    // Release on a fresh, unheld inhibitor must be a no-op (no underflow,
    // no surprise SetThreadExecutionState call).
    inh.release();
    REQUIRE_FALSE(inh.isHeld());

    inh.acquire(QStringLiteral("test"));
    inh.release();
    inh.release(); // Double-release: still no-op.
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
    // RAII: dropping the inhibitor while held must clear the flag so a
    // forgotten release on shutdown doesn't pin the display awake until
    // the next reboot. We can't query the system state directly from
    // unit tests (no public API for "is ES_DISPLAY_REQUIRED set process-wide")
    // but we can at least pin that the dtor path doesn't crash and that
    // a freshly-constructed successor starts in the unheld state again.
    {
        SetThreadExecutionStateInhibitor inh;
        inh.acquire(QStringLiteral("dies on scope exit"));
        REQUIRE(inh.isHeld());
        // dtor runs here.
    }
    const SetThreadExecutionStateInhibitor next;
    REQUIRE_FALSE(next.isHeld());
}
