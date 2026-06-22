// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for dish::source::OnboardingPreferenceStore (Workstream 3a). There is
// NO android @Test for this store (it is a plain AbstractStateSource); these
// cases pin the RULE its android code expresses — welcomeCompleted /
// dashboardHintDismissed default false (first run shows the pager), the command
// methods flip + persist, the flags are independent, resetWelcome clears both,
// and emission is distinct-until-changed. Modelled on test_feature_settings.cpp:
// a QTemporaryDir-backed QSettings is injected, and a relaunch is simulated by a
// second instance over the same ini. Distinct-until-changed is asserted via the
// kernel StateSourceProbe (the recorded emission sequence).

#include "source/store/OnboardingPreferenceStore.h"

#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>

using dish::source::OnboardingPreferenceStore;
using dish::source::OnboardingState;
using dish::test::StateSourceProbe;

namespace {

std::unique_ptr<OnboardingPreferenceStore> makeStore(const QTemporaryDir& dir) {
    const QString path = dir.filePath(QStringLiteral("onboarding.ini"));
    auto settings = std::make_unique<QSettings>(path, QSettings::IniFormat);
    return std::make_unique<OnboardingPreferenceStore>(std::move(settings));
}

} // namespace

TEST_CASE("OnboardingPreferenceStore: fresh store has both flags false", "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);
    REQUIRE_FALSE(store->welcomeCompleted());
    REQUIRE_FALSE(store->dashboardHintDismissed());
}

TEST_CASE("OnboardingPreferenceStore: markWelcomeCompleted flips + persists",
          "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        auto store = makeStore(dir);
        store->markWelcomeCompleted();
        REQUIRE(store->welcomeCompleted());
        // The other flag is untouched.
        REQUIRE_FALSE(store->dashboardHintDismissed());
    }
    // Relaunch: a second instance over the same ini reads true.
    {
        auto reloaded = makeStore(dir);
        REQUIRE(reloaded->welcomeCompleted());
        REQUIRE_FALSE(reloaded->dashboardHintDismissed());
    }
}

TEST_CASE("OnboardingPreferenceStore: dismissDashboardHint is independent of welcome",
          "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);
    store->dismissDashboardHint();
    REQUIRE(store->dashboardHintDismissed());
    // welcomeCompleted must NOT have been touched.
    REQUIRE_FALSE(store->welcomeCompleted());
}

TEST_CASE("OnboardingPreferenceStore: dashboardHintDismissed persists across relaunch",
          "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    {
        auto store = makeStore(dir);
        store->dismissDashboardHint();
    }
    {
        auto reloaded = makeStore(dir);
        REQUIRE(reloaded->dashboardHintDismissed());
    }
}

TEST_CASE("OnboardingPreferenceStore: resetWelcome clears both flags", "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);
    store->markWelcomeCompleted();
    store->dismissDashboardHint();
    REQUIRE(store->welcomeCompleted());
    REQUIRE(store->dashboardHintDismissed());

    store->resetWelcome();
    REQUIRE_FALSE(store->welcomeCompleted());
    REQUIRE_FALSE(store->dashboardHintDismissed());
}

TEST_CASE("OnboardingPreferenceStore: emission is distinct-until-changed", "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);

    StateSourceProbe<OnboardingState> probe(store->state());
    // 1 emission so far: the current value replayed to the new subscriber.
    REQUIRE(probe.count() == 1);

    store->markWelcomeCompleted(); // real transition
    REQUIRE(probe.count() == 2);

    store->markWelcomeCompleted(); // redundant — no re-emit
    REQUIRE(probe.count() == 2);

    store->dismissDashboardHint(); // a different field changes
    REQUIRE(probe.count() == 3);

    store->dismissDashboardHint(); // redundant — no re-emit
    REQUIRE(probe.count() == 3);
}

TEST_CASE("OnboardingPreferenceStore: resetWelcome after no-op does not emit",
          "[onboarding][store]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    auto store = makeStore(dir);

    StateSourceProbe<OnboardingState> probe(store->state());
    REQUIRE(probe.count() == 1);
    // A fresh store is already {false,false}; resetWelcome sets {false,false} —
    // distinct-until-changed suppresses the re-emit.
    store->resetWelcome();
    REQUIRE(probe.count() == 1);
}
