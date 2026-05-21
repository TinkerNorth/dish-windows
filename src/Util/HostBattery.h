// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <cstdint>

namespace dish::util {

// Host-machine battery fallback for the MSG_BATTERY (0x000B) stream.
//
// SDLGamepadBridge reports the *controller's* own battery whenever the pad
// exposes a usable percentage (a wireless DualSense / Switch Pro at LOW /
// MEDIUM / FULL). When the pad is wired (USB) or SDL can't read a level, the
// controller's battery is meaningless — the player wants to know the *host*
// machine's charge instead. readHostBattery() supplies that fallback: on a
// laptop it returns the system battery percentage + charging state; on a
// desktop (no internal battery) it returns 100 % / WIRED so the satellite
// shows a full charge.
//
// The (level, status) pair is the same shape SDLGamepadBridge's
// powerLevelToWire produces and feeds straight into MSG_BATTERY. `level` is
// 0..100 percent or kBatteryLevelUnknown (0xFF); `status` is one of the
// kBatteryStatus* constants — the satellite/src/core/types.h mirrors.
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

// The handful of SYSTEM_POWER_STATUS fields the mapping below needs, lifted
// into a plain struct so the (raw inputs → wire) logic is unit-testable
// without a live Win32 call. Field names + value semantics mirror the Win32
// SYSTEM_POWER_STATUS documentation exactly:
//   * acLineStatus     — 0 offline, 1 online, 255 unknown.
//   * batteryFlag      — bitfield: 1 high, 2 low, 4 critical, 8 charging,
//                        128 "no system battery", 255 unknown.
//   * batteryLifePercent — 0..100, or 255 when unknown.
struct SystemPowerSnapshot {
    std::uint8_t acLineStatus = 255;
    std::uint8_t batteryFlag = 255;
    std::uint8_t batteryLifePercent = 255;
};

// Pure mapping: SystemPowerSnapshot → BatteryReading. No Win32 dependency, so
// unit tests can pin every branch. Rules:
//   * batteryFlag == 128 ("no system battery", i.e. a desktop) → 100 / WIRED.
//   * batteryLifePercent == 255 (unknown) → level kept as 0xFF.
//   * AC online + charging bit (batteryFlag & 8) → CHARGING.
//   * AC online + level at/near 100 %        → FULL.
//   * otherwise                               → DISCHARGING.
BatteryReading hostBatteryFromSnapshot(const SystemPowerSnapshot& snap);

// Query the host machine's battery via GetSystemPowerStatus and run the
// reading through hostBatteryFromSnapshot. On a desktop this returns
// {100, WIRED}; on a laptop the live percentage + charging state. If the
// Win32 call fails outright, falls back to {100, WIRED} — the same value a
// battery-less host would report, so the satellite still sees a sane sample.
BatteryReading readHostBattery();

} // namespace dish::util
