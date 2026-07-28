// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MainUiStateTest (PURE) — the battery-display projection arm. The streaming-slot
// arm of dish-android ui/main/MainUiStateTest is pinned by the streamingSlotCount
// reducer (test_screen_wake_controller / test_wake_state_composer); this file
// pins the OTHER arm the android test documents: BatteryUi.fromWire(level,status)
// and BatteryUi.isLow.
//
// The SoC debt this file used to carry (a tests-local duplicate of the rule,
// flagged in tests/PARITY.md) is closed: the canonical symbols now live in the
// production core/reducer/BatteryUi.h, and these cases exercise THAT header
// directly. The android rule they pin is unchanged — in particular the
// INCLUSIVE <= 15 low threshold (the old inline widget rule was exclusive) and
// the wired->charging fold (the old rule carried an extra !wired term). The
// chip projection built on top of these symbols is pinned by test_battery_ui.cpp.
//
// The wire status constants come from the production core/reducer/BatteryRouting.h
// (they already match android's BatteryValidator: 0 unknown, 1 discharging,
// 2 charging, 3 full, 4 wired; level 0xFF = unknown).

#include "core/reducer/BatteryUi.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

namespace br = dish::reducer;

// ── fromWire ──────────────────────────────────────────────────────────────────

TEST_CASE("MainUiState battery: fromWire keeps a known level and discharging status",
          "[mainui][battery]") {
    const auto b = br::batteryUiFromWire(/*level=*/80, br::kBatteryStatusDischarging);
    REQUIRE(b.has_value());
    REQUIRE(b->level == 80);
    REQUIRE_FALSE(b->charging);
}

TEST_CASE("MainUiState battery: fromWire marks charging/full/wired states as charging",
          "[mainui][battery]") {
    REQUIRE(br::batteryUiFromWire(50, br::kBatteryStatusCharging)->charging);
    REQUIRE(br::batteryUiFromWire(100, br::kBatteryStatusFull)->charging);
    REQUIRE(br::batteryUiFromWire(0xFF, br::kBatteryStatusWired)->charging); // wired desktop pad
}

TEST_CASE("MainUiState battery: fromWire collapses the unknown-level unknown-status pair to null",
          "[mainui][battery]") {
    // Nothing to render: no percentage and no status.
    REQUIRE_FALSE(br::batteryUiFromWire(0xFF, br::kBatteryStatusUnknown).has_value());
}

TEST_CASE("MainUiState battery: fromWire keeps an unknown level when the status is known",
          "[mainui][battery]") {
    // A wired desktop pad reports level 0xFF but a real (wired) status — still
    // renders (as charging), just without a percentage.
    const auto b = br::batteryUiFromWire(0xFF, br::kBatteryStatusWired);
    REQUIRE(b.has_value());
    REQUIRE_FALSE(b->level.has_value());
    REQUIRE(b->charging);
}

// ── isLow ─────────────────────────────────────────────────────────────────────

TEST_CASE("MainUiState battery: isLow is true only for a low non-charging battery",
          "[mainui][battery]") {
    // Low + discharging -> low.
    REQUIRE(br::isLowBattery(br::BatteryUi{15, false}));
    REQUIRE(br::isLowBattery(br::BatteryUi{1, false}));
    // At the threshold boundary android is INCLUSIVE (<= 15) — the exact
    // off-by-one the old inline widget rule got wrong.
    REQUIRE(br::isLowBattery(br::BatteryUi{br::kLowBatteryThreshold, false}));
    // Above the threshold -> not low.
    REQUIRE_FALSE(br::isLowBattery(br::BatteryUi{16, false}));
    REQUIRE_FALSE(br::isLowBattery(br::BatteryUi{100, false}));
    // Charging at a low level -> not low (it's filling up).
    REQUIRE_FALSE(br::isLowBattery(br::BatteryUi{5, true}));
    // Unknown level -> not low (cannot judge).
    REQUIRE_FALSE(br::isLowBattery(br::BatteryUi{std::nullopt, false}));
}

TEST_CASE("MainUiState battery: a low wired pad is not low (wired folds to charging)",
          "[mainui][battery]") {
    // fromWire a low level on a wired pad -> charging -> not low.
    const auto b = br::batteryUiFromWire(10, br::kBatteryStatusWired);
    REQUIRE(b.has_value());
    REQUIRE_FALSE(br::isLowBattery(*b));
}
