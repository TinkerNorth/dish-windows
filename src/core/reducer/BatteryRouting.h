// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure battery-routing + validation for the MSG_BATTERY (0x000B) telemetry path
// (Workstream 2e). Free functions, Qt-free — the analogue of dish-android
// source/sensor/BatteryValidator.kt, BatteryRouting.kt and
// PhysicalBatteryMapping.kt.
//
// dish-windows is AHEAD of android here and STAYS ahead: where android's
// fallback when the pad has no usable battery is the *phone* battery, Windows
// falls back to the *host/laptop* battery (util::HostBattery / readHostBattery).
// This header is the pure decision layer that picks WHICH (level, status) pair
// goes on the wire; the live host query + the SDL power-level read stay in
// SDLGamepadBridge::pollBatteries (its 30 s cadence + forward-every-sample
// behaviour is untouched). The "phone arm → host arm" swap is the only
// intentional delta vs android's BatteryRouting.
//
// Wire constants mirror satellite/src/core/types.h (and Util/HostBattery.h):
//   level  ∈ [0,100] ∪ {0xFF}  (0xFF = unknown sentinel)
//   status ∈ [0,4]             (0 unknown,1 discharging,2 charging,3 full,4 wired)

#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace dish::reducer {

// One (level, status) battery sample — the shape that feeds MSG_BATTERY.
// Mirrors BatteryValidator.BatterySample on android and util::BatteryReading on
// Windows (kept as its own type so core/reducer stays Qt/Util-free).
struct BatterySample {
    std::uint8_t level = 0;
    std::uint8_t status = 0;

    bool operator==(const BatterySample& o) const { return level == o.level && status == o.status; }
};

// ── Wire constants (BatteryValidator companion) ──────────────────────────────
inline constexpr std::uint8_t kBatteryLevelUnknown = 0xFF;
inline constexpr std::uint8_t kBatteryStatusUnknown = 0;
inline constexpr std::uint8_t kBatteryStatusDischarging = 1;
inline constexpr std::uint8_t kBatteryStatusCharging = 2;
inline constexpr std::uint8_t kBatteryStatusFull = 3;
inline constexpr std::uint8_t kBatteryStatusWired = 4;
inline constexpr std::uint8_t kBatteryStatusMin = kBatteryStatusUnknown;
inline constexpr std::uint8_t kBatteryStatusMax = kBatteryStatusWired;
// The 30 s heartbeat cadence (no coalescing — every sample is forwarded so a
// lost UDP packet self-heals on the next tick). Pinned here as documentation;
// the SDL bridge owns the actual timer.
inline constexpr int kBatteryReportIntervalSeconds = 30;

// The all-unknown sample used when neither the pad nor the host yields a
// reading. Mirrors BatteryRouting.UNKNOWN_SAMPLE.
inline constexpr BatterySample kUnknownBatterySample{kBatteryLevelUnknown, kBatteryStatusUnknown};

// ── BatteryValidator ─────────────────────────────────────────────────────────
// True iff the sample is acceptable for the wire: level in [0,100] or the 0xFF
// unknown sentinel, and status in [0,4]. Anything else (bogus 101..254 level, an
// undocumented status) is rejected. Ported from BatteryValidator.publish's
// guard. There is NO coalescing here — validity is independent of the previous
// sample; a valid sample is always forwarded (the 30 s heartbeat rule).
inline bool batterySampleValid(const BatterySample& s) {
    const bool levelOk = (s.level <= 100) || (s.level == kBatteryLevelUnknown);
    const bool statusOk = (s.status >= kBatteryStatusMin) && (s.status <= kBatteryStatusMax);
    return levelOk && statusOk;
}

// Same guard taking raw ints, so a caller validating values before they are
// narrowed to uint8 (e.g. a negative or >255 level from an upstream decode)
// gets the right reject. A negative or >0xFF value is never valid.
inline bool batterySampleValid(int level, int status) {
    if (level < 0 || level > 0xFF) { return false; }
    if (status < 0 || status > 0xFF) { return false; }
    return batterySampleValid(
        BatterySample{static_cast<std::uint8_t>(level), static_cast<std::uint8_t>(status)});
}

// ── physicalBatteryMapping ───────────────────────────────────────────────────
// Android BatteryState.STATUS_* mirror, kept framework-free (the values differ
// from the wire status, so the map below is not the identity).
inline constexpr int kAndroidStatusUnknown = 1;
inline constexpr int kAndroidStatusCharging = 2;
inline constexpr int kAndroidStatusDischarging = 3;
inline constexpr int kAndroidStatusNotCharging = 4;
inline constexpr int kAndroidStatusFull = 5;

// Map a platform battery status code to the wire status. Ported from
// PhysicalBatteryMapping.statusToWire: NOT_CHARGING (plugged-but-held) reports
// as discharging to match player perception; anything outside the documented
// set falls back to unknown.
inline std::uint8_t physicalBatteryStatusToWire(int status) {
    switch (status) {
    case kAndroidStatusCharging:
        return kBatteryStatusCharging;
    case kAndroidStatusFull:
        return kBatteryStatusFull;
    case kAndroidStatusDischarging:
    case kAndroidStatusNotCharging:
        return kBatteryStatusDischarging;
    default:
        return kBatteryStatusUnknown;
    }
}

// Map a physical pad's raw battery reading to a wire sample, or nullopt when the
// pad has no battery present (the caller then uses the host fallback — android
// uses the phone). Ported from PhysicalBatteryMapping.controllerSample:
//   * not present                  → nullopt
//   * NaN / negative capacity      → level 0xFF (unknown sentinel)
//   * otherwise capacity 0.0..1.0  → (capacity*100) truncated, clamped [0,100]
// `capacity` is a fraction in [0,1]; `status` is a platform status code.
inline std::optional<BatterySample> physicalBatteryMapping(bool isPresent, float capacity,
                                                           int status) {
    if (!isPresent) { return std::nullopt; }
    std::uint8_t level = kBatteryLevelUnknown;
    if (!std::isnan(capacity) && capacity >= 0.0f) {
        int pct = static_cast<int>(capacity * 100.0f); // truncates, like Kotlin's toInt()
        if (pct < 0) { pct = 0; }
        if (pct > 100) { pct = 100; }
        level = static_cast<std::uint8_t>(pct);
    }
    return BatterySample{level, physicalBatteryStatusToWire(status)};
}

// ── resolveBattery (BatteryRouting.route, phone arm → host arm) ──────────────
// The result of routing: what to DISPLAY (the pad's own reading, if any) and
// what to put on the WIRE. Mirrors BatteryRouting.Routed.
struct RoutedBattery {
    std::optional<BatterySample> display; // the pad's reading for the UI chip
    BatterySample wire;                   // what MSG_BATTERY carries
};

// Treat the unknown sentinel as +infinity so a known level always wins the
// lowest-pick. Mirrors BatteryRouting.comparableLevel.
inline int comparableBatteryLevel(const BatterySample& s) {
    return s.level == kBatteryLevelUnknown ? std::numeric_limits<int>::max() : s.level;
}

// The lower of pad vs host. A null host yields the pad unchanged. On a tie the
// pad wins (deterministic). Ported from BatteryRouting.lowest (phone→host).
inline BatterySample lowestBattery(const BatterySample& pad,
                                   const std::optional<BatterySample>& host) {
    if (!host.has_value()) { return pad; }
    return comparableBatteryLevel(*host) < comparableBatteryLevel(pad) ? *host : pad;
}

// Decide the wire + display battery. `padWired` is true when the pad is wired /
// reports no usable own battery (android: Transport.Usb) — in that case the host
// battery always goes on the wire (a wired pad has no own charge to report),
// while the pad reading (if any) is still shown. For a wireless pad the lowest
// of (pad, host) wins the wire. When the pad has no reading at all, the host
// wins; when neither side has a reading, the unknown sentinel goes out.
// Ported from BatteryRouting.route with the phone arm replaced by host.
inline RoutedBattery resolveBattery(bool padWired, const std::optional<BatterySample>& pad,
                                    const std::optional<BatterySample>& host) {
    BatterySample wire;
    if (!pad.has_value() || padWired) {
        wire = host.value_or(kUnknownBatterySample);
    } else {
        wire = lowestBattery(*pad, host);
    }
    return RoutedBattery{pad, wire};
}

} // namespace dish::reducer
