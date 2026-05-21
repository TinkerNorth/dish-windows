// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

// Coverage for util::hostBatteryFromSnapshot — the pure mapping from the
// SYSTEM_POWER_STATUS fields to the MSG_BATTERY (0x000B) wire (level, status)
// pair. The live readHostBattery() wraps this around a GetSystemPowerStatus
// call; the mapping is split out so every branch is testable without driving
// the Win32 API. Same pattern as test_satellite_client_motion.cpp — the pure
// function is the seam.

#include "Util/HostBattery.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using dish::util::BatteryReading;
using dish::util::hostBatteryFromSnapshot;
using dish::util::SystemPowerSnapshot;

namespace {

// SYSTEM_POWER_STATUS field values used across the cases.
constexpr std::uint8_t kAcOffline = 0;
constexpr std::uint8_t kAcOnline = 1;
constexpr std::uint8_t kAcUnknown = 255;
constexpr std::uint8_t kFlagHigh = 1;
constexpr std::uint8_t kFlagLow = 2;
constexpr std::uint8_t kFlagCharging = 8;
constexpr std::uint8_t kFlagNoBattery = 128;
constexpr std::uint8_t kFlagUnknown = 255;
constexpr std::uint8_t kPercentUnknown = 255;

} // namespace

TEST_CASE("desktop (BatteryFlag 128) reports 100% wired", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = kFlagNoBattery;
    snap.batteryLifePercent = kPercentUnknown;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 100U);
    REQUIRE(r.status == dish::util::kBatteryStatusWired);
}

TEST_CASE("laptop discharging on battery reports the percentage", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOffline;
    snap.batteryFlag = kFlagHigh;
    snap.batteryLifePercent = 78;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 78U);
    REQUIRE(r.status == dish::util::kBatteryStatusDischarging);
}

TEST_CASE("laptop on AC with the charging bit reports charging", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = static_cast<std::uint8_t>(kFlagLow | kFlagCharging);
    snap.batteryLifePercent = 42;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 42U);
    REQUIRE(r.status == dish::util::kBatteryStatusCharging);
}

TEST_CASE("laptop on AC near 100% with no charging bit reports full", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = kFlagHigh; // charging bit clear
    snap.batteryLifePercent = 100;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 100U);
    REQUIRE(r.status == dish::util::kBatteryStatusFull);
}

TEST_CASE("99% on AC counts as full (Windows tops out a couple points shy)", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = kFlagHigh;
    snap.batteryLifePercent = 99;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.status == dish::util::kBatteryStatusFull);
}

TEST_CASE("on AC, charging bit clear, not near full reports discharging", "[hostbattery]") {
    // Rare but real: some firmware briefly drops the charging bit mid-charge.
    // The mapping treats it as discharging so the UI never shows a stale
    // "charging" — it self-corrects on the next 30 s poll.
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = kFlagHigh;
    snap.batteryLifePercent = 55;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 55U);
    REQUIRE(r.status == dish::util::kBatteryStatusDischarging);
}

TEST_CASE("unknown battery percentage maps to the 0xFF sentinel", "[hostbattery]") {
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcUnknown;
    snap.batteryFlag = kFlagUnknown;
    snap.batteryLifePercent = kPercentUnknown;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == dish::util::kBatteryLevelUnknown);
    // Status with no AC info and an unknown flag falls through to discharging
    // — the most conservative non-charging assumption.
    REQUIRE(r.status == dish::util::kBatteryStatusDischarging);
}

TEST_CASE("low battery on AC + charging keeps the level intact", "[hostbattery]") {
    // The level is reported verbatim regardless of status — the SlotCard's
    // low-battery styling is a UI decision, not a wire one.
    SystemPowerSnapshot snap;
    snap.acLineStatus = kAcOnline;
    snap.batteryFlag = static_cast<std::uint8_t>(kFlagLow | kFlagCharging);
    snap.batteryLifePercent = 7;
    const auto r = hostBatteryFromSnapshot(snap);
    REQUIRE(r.level == 7U);
    REQUIRE(r.status == dish::util::kBatteryStatusCharging);
}
