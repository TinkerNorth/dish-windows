// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <cstdint>

namespace dish::util {

// Host-machine battery fallback for MSG_BATTERY, used when the pad is wired or
// SDL cannot read its level (the controller's own reading is meaningless then).
//
// Wire shape: `level` is 0..100 percent or kBatteryLevelUnknown (0xFF),
// `status` one of the kBatteryStatus* constants. Both must match
// satellite/src/core/types.h.
struct BatteryReading {
    std::uint8_t level;
    std::uint8_t status;
};

inline constexpr std::uint8_t kBatteryLevelUnknown = 0xFF;
inline constexpr std::uint8_t kBatteryStatusUnknown = 0;
inline constexpr std::uint8_t kBatteryStatusDischarging = 1;
inline constexpr std::uint8_t kBatteryStatusCharging = 2;
inline constexpr std::uint8_t kBatteryStatusFull = 3;
inline constexpr std::uint8_t kBatteryStatusWired = 4;

// SYSTEM_POWER_STATUS lifted into a plain struct so the mapping is testable
// without a live Win32 call. Value semantics are Win32's:
//   * acLineStatus       — 0 offline, 1 online, 255 unknown.
//   * batteryFlag        — bits: 1 high, 2 low, 4 critical, 8 charging,
//                          128 "no system battery", 255 unknown.
//   * batteryLifePercent — 0..100, or 255 when unknown.
struct SystemPowerSnapshot {
    std::uint8_t acLineStatus = 255;
    std::uint8_t batteryFlag = 255;
    std::uint8_t batteryLifePercent = 255;
};

BatteryReading hostBatteryFromSnapshot(const SystemPowerSnapshot& snap);

// GetSystemPowerStatus + the mapping above. A failed Win32 call falls back to
// the battery-less-host value so MSG_BATTERY still carries a sane sample.
BatteryReading readHostBattery();

} // namespace dish::util
