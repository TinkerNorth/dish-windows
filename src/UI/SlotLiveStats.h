// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotLiveStats — the pure decision layer behind the slot card's live-stats
// chips (the small "120 Hz" / "~60 Hz" readouts). Mirrors dish-android
// ControllerAdapter.controllerRateText / gyroRatePill: given a slot's measured
// rates and whether it is a USB-direct pad, decide WHICH chip text to show and
// HOW (a live value vs. a peak "~" value vs. nothing).
//
// Qt-free + allocation-free so the mapping is unit-testable without a widget:
// the SlotCard takes these structured results and formats the final localized
// string ("%1 Hz" / "~%1 Hz") via tr(), keeping the i18n in the widget and the
// rules here. Mirrors the android rule split (adapter decides, resources format).

#pragma once

#include "Models/Models.h"

namespace dish::ui {

// How a rate chip should render. Hidden chips are simply not shown (android omits
// the pill / shows the capability-off pill); the slot card maps Live/Peak to the
// "%1 Hz" / "~%1 Hz" strings respectively.
enum class RateChipKind {
    Hidden, // no measurement worth showing (idle, never measured)
    Live,   // a current measurement: "<hz> Hz"
    Peak,   // only a high-water mark: "~<hz> Hz"
};

struct RateChip {
    RateChipKind kind = RateChipKind::Hidden;
    int hz = 0;
    bool operator==(const RateChip& o) const { return kind == o.kind && hz == o.hz; }
    bool operator!=(const RateChip& o) const { return !(*this == o); }
};

// The gamepad (button/stick) rate chip. Mirrors android controllerRateText:
//   * a USB-direct pad streams continuously, so a live gamepadHz reads as the
//     measurement -> Live.
//   * otherwise the pad only delivers reports while the user is actuating it, so
//     the peak window approximates the rate and renders with a "~" -> Peak.
//   * nothing measured yet -> Hidden.
inline RateChip gamepadRateChip(const models::SlotLiveRates& r, bool usbDirect) {
    if (usbDirect && r.gamepadHz > 0) { return RateChip{RateChipKind::Live, r.gamepadHz}; }
    if (r.gamepadPeakHz > 0) { return RateChip{RateChipKind::Peak, r.gamepadPeakHz}; }
    return RateChip{RateChipKind::Hidden, 0};
}

// The motion (gyro/accel) rate chip. Mirrors android gyroRatePill: a live gyroHz
// reads directly (motion is genuinely event-driven from the IMU, no "~"); absent
// a reading the chip is hidden. Only meaningful for a motion-capable pad — the
// caller gates on hasMotion before showing it.
inline RateChip motionRateChip(const models::SlotLiveRates& r) {
    if (r.motionHz > 0) { return RateChip{RateChipKind::Live, r.motionHz}; }
    return RateChip{RateChipKind::Hidden, 0};
}

// The USB-direct poll-rate chip (the independently-measured URB completion rate).
// Shown only for a USB-direct pad with a positive measurement; always a live
// value (the poll loop runs continuously while claimed). Android surfaces the
// equivalent directPollHz on the path card.
inline RateChip pollRateChip(const models::SlotLiveRates& r, bool usbDirect) {
    if (usbDirect && r.directPollHz > 0) { return RateChip{RateChipKind::Live, r.directPollHz}; }
    return RateChip{RateChipKind::Hidden, 0};
}

} // namespace dish::ui
