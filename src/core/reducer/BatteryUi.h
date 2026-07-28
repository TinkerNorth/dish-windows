// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// BatteryUi — the CANONICAL battery-display projection, pure and Qt-free. Port
// of dish-android ui/main/MainUiState.kt's BatteryUi (fromWire + isLow +
// LOW_THRESHOLD).
//
// Why this home exists: both Windows UIs used to duplicate the low-battery rule
// inline (UI/SlotCard.cpp and qml/pages/ControllersPage.qml), and BOTH copies
// diverged from android's canonical rule — they used `< 15` (android is
// inclusive, `<= 15`) and added an extra `!wired` term android expresses by
// folding wired into `charging`. Lifting the rule here makes the two UIs agree
// with each other and with android by construction; the chip projection below
// emits RENDER TOKENS (an enum + numbers), never user strings, so localization
// stays in the view layer.
//
// Wire constants come from core/reducer/BatteryRouting.h (the BatteryValidator
// mirror): level 0xFF = unknown; status 0 unknown / 1 discharging / 2 charging /
// 3 full / 4 wired.

#pragma once

#include "core/reducer/BatteryRouting.h"

#include <optional>

namespace dish::reducer {

// android BatteryUi.LOW_THRESHOLD — INCLUSIVE (a 15 % pad is low).
inline constexpr int kLowBatteryThreshold = 15;

// The android BatteryUi value. `level` empty means "unknown percentage";
// `charging` folds the charging/full/wired wire statuses (a wired pad has
// nothing to drain, so it can never read as low).
struct BatteryUi {
    std::optional<int> level;
    bool charging = false;

    bool operator==(const BatteryUi& o) const { return level == o.level && charging == o.charging; }
    bool operator!=(const BatteryUi& o) const { return !(*this == o); }
};

// android BatteryUi.isLow, exactly: a KNOWN, non-charging level at or below the
// threshold. Unknown level -> not low (cannot judge); charging at any level ->
// not low (it's filling up).
inline bool isLowBattery(const BatteryUi& b) {
    return b.level.has_value() && !b.charging && *b.level <= kLowBatteryThreshold;
}

// android BatteryUi.fromWire: empty only when BOTH level and status are unknown
// (nothing to render). Otherwise charging = (charging|full|wired) and the 0xFF
// level sentinel drops the percentage to empty — a wired desktop pad still
// renders (as charging), just without a number.
inline std::optional<BatteryUi> batteryUiFromWire(int level, int status) {
    const bool charging = status == kBatteryStatusCharging || status == kBatteryStatusFull ||
                          status == kBatteryStatusWired;
    const std::optional<int> pct =
        (level == kBatteryLevelUnknown) ? std::nullopt : std::optional<int>(level);
    if (!pct.has_value() && status == kBatteryStatusUnknown) { return std::nullopt; }
    return BatteryUi{pct, charging};
}

// ── Chip projection ──────────────────────────────────────────────────────────
// What the per-slot battery chip should render, as tokens. The view layer maps
// each kind to its localized string ("Battery %1% ↑" / "Battery wired" / ...);
// keeping the branch HERE is what stops the widget and QML paths disagreeing.
enum class BatteryChipKind {
    None,     // no chip at all — the level is unknown, nothing meaningful to show
    Wired,    // "wired" (host has no internal battery); level not rendered
    Charging, // "charging", rendered WITH the level
    Full,     // "full"; level not rendered
    Level,    // plain percentage, with `low` driving the warning style
};

struct BatteryChip {
    BatteryChipKind kind = BatteryChipKind::None;
    int level = 0;    // meaningful for Charging and Level
    bool low = false; // true only in the Level arm — the canonical isLowBattery
                      // verdict (charging/full/wired can never be low)

    bool operator==(const BatteryChip& o) const {
        return kind == o.kind && level == o.level && low == o.low;
    }
    bool operator!=(const BatteryChip& o) const { return !(*this == o); }
};

// Project one (level, status) wire sample to its chip. An unknown level (0xFF)
// hides the chip regardless of status — a reading hasn't landed yet, and a bare
// status with no percentage isn't worth a pill (this matches both existing
// Windows UIs; android renders its slot rows from the same no-number fold).
inline BatteryChip batteryChip(int level, int status) {
    if (level == kBatteryLevelUnknown) { return BatteryChip{}; }
    BatteryChip chip;
    chip.level = level;
    if (status == kBatteryStatusWired) {
        chip.kind = BatteryChipKind::Wired;
    } else if (status == kBatteryStatusCharging) {
        chip.kind = BatteryChipKind::Charging;
    } else if (status == kBatteryStatusFull) {
        chip.kind = BatteryChipKind::Full;
    } else {
        // Discharging / unknown status: a plain percentage. The low flag is the
        // canonical android rule — inclusive at the threshold (<= 15).
        chip.kind = BatteryChipKind::Level;
        chip.low = isLowBattery(BatteryUi{level, /*charging=*/false});
    }
    return chip;
}

} // namespace dish::reducer
