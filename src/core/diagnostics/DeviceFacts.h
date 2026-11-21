// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The PC's own facts that explain a latency or feedback complaint
// (docs/diagnostics-bundle.md, "Device facts"): its battery and whether
// battery saver is on, read from GetSystemPowerStatus's four bytes, which the
// system layer passes in as they came. Windows names no thermal state to an
// app, and this client does not link the WLAN API, so neither is here.

#pragma once

#include <cstdint>
#include <optional>

namespace dish::diagnostics {

// SYSTEM_POWER_STATUS as the call filled it.
struct PowerStatusBytes {
    std::uint8_t acLineStatus = 0;
    std::uint8_t batteryFlag = 0;
    std::uint8_t batteryLifePercent = 0;
    std::uint8_t systemStatusFlag = 0;
};

struct BatteryFacts {
    std::optional<int> level;
    bool charging = false;
};

struct PowerFacts {
    // Empty on a PC with no system battery, or one Windows cannot read.
    std::optional<BatteryFacts> battery;
    std::optional<bool> powerSave;
};

// winbase.h's values, named so the reading below says what it tests.
inline constexpr std::uint8_t kBatteryFlagCharging = 8;
inline constexpr std::uint8_t kBatteryFlagNoSystemBattery = 128;
inline constexpr std::uint8_t kBatteryFlagUnknown = 255;
inline constexpr std::uint8_t kBatteryPercentUnknown = 255;
inline constexpr std::uint8_t kSystemStatusSaverOn = 1;

// `status` is empty when the call failed.
inline PowerFacts powerFactsOf(const std::optional<PowerStatusBytes>& status) {
    PowerFacts facts;
    if (!status.has_value()) { return facts; }
    facts.powerSave = status->systemStatusFlag == kSystemStatusSaverOn;
    const bool noBattery = status->batteryFlag == kBatteryFlagNoSystemBattery ||
                           status->batteryFlag == kBatteryFlagUnknown;
    if (noBattery) { return facts; }
    BatteryFacts battery;
    if (status->batteryLifePercent != kBatteryPercentUnknown) {
        battery.level = static_cast<int>(status->batteryLifePercent);
    }
    battery.charging = (status->batteryFlag & kBatteryFlagCharging) != 0;
    facts.battery = battery;
    return facts;
}

} // namespace dish::diagnostics
