// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for Workstream 3b: the donate-pill 24h dismissal rule. No android
// @Test exists (DonatePill.kt is UI-only); these pin the rule its code
// expresses — `System.currentTimeMillis() - dismissedAt < WINDOW` — re-expressed
// as the clock-free pure function donatePillSuppressed(dismissedAt, now, window)
// plus a persistence round-trip over an injected QSettings (simulating a
// relaunch). The animation is not under test; the behaviour is the window.

#include "UI/donate/DonatePill.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>

#include <cstdint>

using dish::ui::donatePillReadDismissedAt;
using dish::ui::donatePillSuppressed;
using dish::ui::donatePillWriteDismissedAt;
using dish::ui::kDonatePillDismissWindowMs;

TEST_CASE("donate pill: just-dismissed (now == dismissedAt) is suppressed", "[donate][pill]") {
    const std::int64_t t = 1'700'000'000'000LL;
    REQUIRE(donatePillSuppressed(t, t));
}

TEST_CASE("donate pill: within the 24h window is suppressed", "[donate][pill]") {
    const std::int64_t dismissedAt = 1'700'000'000'000LL;
    const std::int64_t now = dismissedAt + kDonatePillDismissWindowMs - 1; // 1 ms short
    REQUIRE(donatePillSuppressed(dismissedAt, now));
}

TEST_CASE("donate pill: exactly at the boundary is NOT suppressed (pill returns)",
          "[donate][pill]") {
    const std::int64_t dismissedAt = 1'700'000'000'000LL;
    const std::int64_t now = dismissedAt + kDonatePillDismissWindowMs; // exactly 24h later
    REQUIRE_FALSE(donatePillSuppressed(dismissedAt, now));
}

TEST_CASE("donate pill: past the boundary is NOT suppressed", "[donate][pill]") {
    const std::int64_t dismissedAt = 1'700'000'000'000LL;
    const std::int64_t now = dismissedAt + kDonatePillDismissWindowMs + 60'000; // a minute past
    REQUIRE_FALSE(donatePillSuppressed(dismissedAt, now));
}

TEST_CASE("donate pill: never-dismissed (dismissedAt == 0) is NOT suppressed", "[donate][pill]") {
    // The realistic 'now' is decades past epoch, far beyond the 24h window from 0.
    const std::int64_t now = 1'700'000'000'000LL;
    REQUIRE_FALSE(donatePillSuppressed(0, now));
}

TEST_CASE("donate pill: persisted dismissedAt survives a relaunch", "[donate][pill]") {
    auto settings = dish::test::makeSharedSettings();
    // Fresh store: no key written -> read as 0 (never dismissed).
    REQUIRE(donatePillReadDismissedAt(*settings) == 0);

    const std::int64_t dismissedAt = 1'700'000'000'000LL;
    donatePillWriteDismissedAt(*settings, dismissedAt);

    // Re-open the same ini (relaunch): the value reads back, and the suppression
    // decision is unchanged across the "relaunch".
    settings->sync();
    QSettings reopened(settings->fileName(), QSettings::IniFormat);
    REQUIRE(donatePillReadDismissedAt(reopened) == dismissedAt);
    REQUIRE(donatePillSuppressed(donatePillReadDismissedAt(reopened), dismissedAt + 1000));
    REQUIRE_FALSE(donatePillSuppressed(donatePillReadDismissedAt(reopened),
                                       dismissedAt + kDonatePillDismissWindowMs));
}
