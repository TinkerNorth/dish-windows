// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "HostBattery.h"

#include <windows.h>

namespace dish::util {

namespace {

// SYSTEM_POWER_STATUS.BatteryFlag bits (winbase.h). Only the two we branch on
// are named here; `128` is the sentinel for a machine with no internal
// battery (a desktop).
constexpr std::uint8_t kBatteryFlagCharging = 8;
constexpr std::uint8_t kBatteryFlagNoBattery = 128;

// SYSTEM_POWER_STATUS.ACLineStatus == 1 means the machine is on AC power.
constexpr std::uint8_t kAcLineOnline = 1;

// At or above this percentage a battery on AC power is treated as FULL rather
// than CHARGING — Windows stops asserting the charging bit a couple of points
// shy of 100 %, so a strict "== 100" test would never report FULL.
constexpr std::uint8_t kFullThresholdPercent = 99;

} // namespace

BatteryReading hostBatteryFromSnapshot(const SystemPowerSnapshot& snap) {
    // Desktop / battery-less host: there is nothing to discharge, so report a
    // full wired charge — same value SDL's WIRED power level mapped to before
    // this fallback existed.
    if (snap.batteryFlag == kBatteryFlagNoBattery) { return {100, kBatteryStatusWired}; }

    const bool acOnline = snap.acLineStatus == kAcLineOnline;
    const bool charging = (snap.batteryFlag & kBatteryFlagCharging) != 0;
    const bool levelKnown = snap.batteryLifePercent != 255;
    const std::uint8_t level = levelKnown ? snap.batteryLifePercent : kBatteryLevelUnknown;

    std::uint8_t status = kBatteryStatusDischarging;
    if (acOnline) {
        if (charging) {
            status = kBatteryStatusCharging;
        } else if (levelKnown && snap.batteryLifePercent >= kFullThresholdPercent) {
            // On AC, not actively charging, sitting at the top of the curve —
            // the battery is full.
            status = kBatteryStatusFull;
        } else {
            // On AC but the charging bit is clear and we're not near 100 %.
            // Rare (some firmware briefly drops the bit mid-charge); treat as
            // discharging so the UI never shows a stale "charging".
            status = kBatteryStatusDischarging;
        }
    }
    return {level, status};
}

BatteryReading readHostBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps) == 0) {
        // The call failed — no usable host reading. Fall back to the
        // battery-less-host value so MSG_BATTERY still carries a sane sample.
        return {100, kBatteryStatusWired};
    }
    SystemPowerSnapshot snap;
    snap.acLineStatus = sps.ACLineStatus;
    snap.batteryFlag = sps.BatteryFlag;
    snap.batteryLifePercent = sps.BatteryLifePercent;
    return hostBatteryFromSnapshot(snap);
}

} // namespace dish::util
