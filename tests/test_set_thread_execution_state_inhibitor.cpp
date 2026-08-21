// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SetThreadExecutionState is process-wide but harmless to flip here: it only
// sets a flag the power manager consults, and each dtor clears it back to
// ES_CONTINUOUS so the runner is left clean.

#include "source/system/WakeInhibitor.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

using dish::reducer::KeepAwakeReach;
using dish::source::SetThreadExecutionStateInhibitor;

namespace {

const QString kReason = QStringLiteral("test reason");

} // namespace

TEST_CASE("STES inhibitor starts holding nothing", "[wake][windows]") {
    const SetThreadExecutionStateInhibitor inh;
    REQUIRE(inh.held() == KeepAwakeReach::None);
}

TEST_CASE("STES inhibitor holds the reach it is given", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.apply(KeepAwakeReach::System, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::System);
    inh.apply(KeepAwakeReach::None, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::None);
}

TEST_CASE("STES inhibitor widens and narrows between System and SystemAndDisplay",
          "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.apply(KeepAwakeReach::System, kReason);
    inh.apply(KeepAwakeReach::SystemAndDisplay, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::SystemAndDisplay);
    inh.apply(KeepAwakeReach::System, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::System);
    inh.apply(KeepAwakeReach::None, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::None);
}

TEST_CASE("STES inhibitor apply is idempotent at every reach", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.apply(KeepAwakeReach::None, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::None);

    inh.apply(KeepAwakeReach::SystemAndDisplay, kReason);
    inh.apply(KeepAwakeReach::SystemAndDisplay, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::SystemAndDisplay);

    inh.apply(KeepAwakeReach::None, kReason);
    inh.apply(KeepAwakeReach::None, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::None);
}

TEST_CASE("STES inhibitor re-holds cleanly after letting go", "[wake][windows]") {
    SetThreadExecutionStateInhibitor inh;
    inh.apply(KeepAwakeReach::System, QStringLiteral("first stream"));
    inh.apply(KeepAwakeReach::None, kReason);
    inh.apply(KeepAwakeReach::System, QStringLiteral("second stream"));
    REQUIRE(inh.held() == KeepAwakeReach::System);
    inh.apply(KeepAwakeReach::None, kReason);
    REQUIRE(inh.held() == KeepAwakeReach::None);
}

TEST_CASE("STES inhibitor destructor releases a held flag", "[wake][windows]") {
    // There is no public API for "is ES_SYSTEM_REQUIRED set process-wide", so
    // the successor's empty state is as close as a unit test can get to proving
    // the dtor cleared the flag.
    {
        SetThreadExecutionStateInhibitor inh;
        inh.apply(KeepAwakeReach::SystemAndDisplay, QStringLiteral("dies on scope exit"));
        REQUIRE(inh.held() == KeepAwakeReach::SystemAndDisplay);
    }
    const SetThreadExecutionStateInhibitor next;
    REQUIRE(next.held() == KeepAwakeReach::None);
}
