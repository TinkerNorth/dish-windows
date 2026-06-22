// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MainUiStateTest (PURE) — the battery-display projection arm. The streaming-slot
// arm of dish-android ui/main/MainUiStateTest is pinned by the streamingSlotCount
// reducer (test_screen_wake_controller / test_wake_state_composer); this file
// pins the OTHER arm the android test documents: BatteryUi.fromWire(level,status)
// and BatteryUi.isLow.
//
// NOTE (SoC debt, flagged in tests/PARITY.md): on Windows the isLow rule today
// lives INLINE inside the QWidget paint path (src/UI/SlotCard.cpp), where it has
// no pure seam AND diverges from android's canonical rule:
//   * SlotCard uses `level < 15`     ; android uses `level <= 15`  (off-by-one at 15)
//   * SlotCard adds an extra `&& !wired` term android does not have
// Rather than introduce a parallel production symbol the widget wouldn't call (or
// refactor READS-ONLY src/), this test pins ANDROID's exact rule via a tests-local
// pure function. It documents the canonical behavior and makes the divergence
// auditable. When Wave 2f lifts the rule into core/reducer/BatteryUi.h and points
// SlotCard at it, replace batteryUiFromWire/isLow below with that real symbol.
//
// The wire status constants come from the production core/reducer/BatteryRouting.h
// (they already match android's BatteryValidator: 0 unknown, 1 discharging,
// 2 charging, 3 full, 4 wired; level 0xFF = unknown).

#include "core/reducer/BatteryRouting.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <optional>

namespace {

namespace br = dish::reducer;

// The android BatteryUi value (ui/main/MainUiState.kt). level == nullopt means
// "unknown percentage"; charging folds the charging/full/wired wire statuses.
struct BatteryUi {
    std::optional<int> level;
    bool charging = false;
    bool operator==(const BatteryUi& o) const { return level == o.level && charging == o.charging; }
};

// android BatteryUi.LOW_THRESHOLD.
constexpr int kLowThreshold = 15;

// android BatteryUi.isLow: a known, non-charging level at or below the threshold.
bool isLow(const BatteryUi& b) {
    return b.level.has_value() && !b.charging && *b.level <= kLowThreshold;
}

// android BatteryUi.fromWire: returns nullopt only when BOTH level and status are
// unknown (nothing to render); otherwise charging = (charging|full|wired) and the
// percentage is dropped to nullopt for the 0xFF level sentinel.
std::optional<BatteryUi> batteryUiFromWire(int level, int status) {
    const bool charging = status == br::kBatteryStatusCharging ||
                          status == br::kBatteryStatusFull || status == br::kBatteryStatusWired;
    const std::optional<int> pct =
        (level == br::kBatteryLevelUnknown) ? std::nullopt : std::optional<int>(level);
    if (!pct.has_value() && status == br::kBatteryStatusUnknown) { return std::nullopt; }
    return BatteryUi{pct, charging};
}

} // namespace

// ── fromWire ──────────────────────────────────────────────────────────────────

TEST_CASE("MainUiState battery: fromWire keeps a known level and discharging status",
          "[mainui][battery]") {
    const auto b = batteryUiFromWire(/*level=*/80, br::kBatteryStatusDischarging);
    REQUIRE(b.has_value());
    REQUIRE(b->level == 80);
    REQUIRE_FALSE(b->charging);
}

TEST_CASE("MainUiState battery: fromWire marks charging/full/wired states as charging",
          "[mainui][battery]") {
    REQUIRE(batteryUiFromWire(50, br::kBatteryStatusCharging)->charging);
    REQUIRE(batteryUiFromWire(100, br::kBatteryStatusFull)->charging);
    REQUIRE(batteryUiFromWire(0xFF, br::kBatteryStatusWired)->charging); // wired desktop pad
}

TEST_CASE("MainUiState battery: fromWire collapses the unknown-level unknown-status pair to null",
          "[mainui][battery]") {
    // Nothing to render: no percentage and no status.
    REQUIRE_FALSE(batteryUiFromWire(0xFF, br::kBatteryStatusUnknown).has_value());
}

TEST_CASE("MainUiState battery: fromWire keeps an unknown level when the status is known",
          "[mainui][battery]") {
    // A wired desktop pad reports level 0xFF but a real (wired) status — still
    // renders (as charging), just without a percentage.
    const auto b = batteryUiFromWire(0xFF, br::kBatteryStatusWired);
    REQUIRE(b.has_value());
    REQUIRE_FALSE(b->level.has_value());
    REQUIRE(b->charging);
}

// ── isLow ─────────────────────────────────────────────────────────────────────

TEST_CASE("MainUiState battery: isLow is true only for a low non-charging battery",
          "[mainui][battery]") {
    // Low + discharging -> low.
    REQUIRE(isLow(BatteryUi{15, false}));
    REQUIRE(isLow(BatteryUi{1, false}));
    // At the threshold boundary android is INCLUSIVE (<= 15). (SlotCard.cpp is
    // exclusive here — the flagged divergence.)
    REQUIRE(isLow(BatteryUi{kLowThreshold, false}));
    // Above the threshold -> not low.
    REQUIRE_FALSE(isLow(BatteryUi{16, false}));
    REQUIRE_FALSE(isLow(BatteryUi{100, false}));
    // Charging at a low level -> not low (it's filling up).
    REQUIRE_FALSE(isLow(BatteryUi{5, true}));
    // Unknown level -> not low (cannot judge).
    REQUIRE_FALSE(isLow(BatteryUi{std::nullopt, false}));
}

TEST_CASE("MainUiState battery: a low wired pad is not low (wired folds to charging)",
          "[mainui][battery]") {
    // fromWire a low level on a wired pad -> charging -> not low.
    const auto b = batteryUiFromWire(10, br::kBatteryStatusWired);
    REQUIRE(b.has_value());
    REQUIRE_FALSE(isLow(*b));
}
