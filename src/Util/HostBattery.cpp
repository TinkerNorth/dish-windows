// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "HostBattery.h"

#include <windows.h>

namespace dish::util {

namespace {

// SYSTEM_POWER_STATUS.BatteryFlag bits (winbase.h).
constexpr std::uint8_t kBatteryFlagCharging = 8;
constexpr std::uint8_t kBatteryFlagNoBattery = 128;

constexpr std::uint8_t kAcLineOnline = 1;

// Windows drops the charging bit a couple of points shy of 100 %, so a strict
// "== 100" test would never report FULL.
constexpr std::uint8_t kFullThresholdPercent = 99;

} // namespace

BatteryReading hostBatteryFromSnapshot(const SystemPowerSnapshot& snap) {
    // A desktop has nothing to discharge; report a full wired charge.
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
            status = kBatteryStatusFull;
        } else {
            // Some firmware briefly drops the charging bit mid-charge; report
            // discharging rather than leaving a stale "charging" on screen.
            status = kBatteryStatusDischarging;
        }
    }
    return {level, status};
}

BatteryReading readHostBattery() {
    SYSTEM_POWER_STATUS sps{};
    if (GetSystemPowerStatus(&sps) == 0) { return {100, kBatteryStatusWired}; }
    SystemPowerSnapshot snap;
    snap.acLineStatus = sps.ACLineStatus;
    snap.batteryFlag = sps.BatteryFlag;
    snap.batteryLifePercent = sps.BatteryLifePercent;
    return hostBatteryFromSnapshot(snap);
}

} // namespace dish::util
