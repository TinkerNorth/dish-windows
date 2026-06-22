// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotInputRates — the per-slot live-rate value the InputRateStore publishes and
// the dashboard's live-stats display reads: the current and peak Hz for each
// input stream the slot carries. Mirrors dish-android source/inputrate/
// SlotInputRates, with the android "screen" (on-screen touchpad) stream dropped
// — Windows forwards a real controller, so the streams are the gamepad button/
// stick reports and the gyro/motion samples.
//
// Pure value type, Qt-free, ==-comparable so it can sit inside an Observable
// map whose distinct-until-changed suppresses no-op re-emits.

#pragma once

namespace dish::source {

struct SlotInputRates {
    // Gamepad report stream (buttons / sticks / triggers): current + peak Hz.
    int gamepadHz = 0;
    int gamepadPeakHz = 0;
    // Motion (gyro / accel) sample stream: current + peak Hz.
    int motionHz = 0;
    int motionPeakHz = 0;

    // True iff any stream has shown activity — either flowing now or having a
    // recorded peak. Drives whether the live-stats row is worth showing for the
    // slot at all. Mirrors android SlotInputRates.hasAny.
    bool hasAny() const {
        return gamepadHz > 0 || gamepadPeakHz > 0 || motionHz > 0 || motionPeakHz > 0;
    }

    bool operator==(const SlotInputRates& o) const {
        return gamepadHz == o.gamepadHz && gamepadPeakHz == o.gamepadPeakHz &&
               motionHz == o.motionHz && motionPeakHz == o.motionPeakHz;
    }
    bool operator!=(const SlotInputRates& o) const { return !(*this == o); }
};

} // namespace dish::source
