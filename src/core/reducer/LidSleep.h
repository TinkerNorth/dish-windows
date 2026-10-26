// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The lid, as far as a sleep is concerned. Dish cannot keep a Windows laptop
// awake with its lid closed: power requests, SetThreadExecutionState included,
// are cleared when the user starts sleep directly, and closing the lid is one
// of those ways (Microsoft's PowerSetRequest remarks). What it can do is say so
// once, after the resume, when a lid-closed sleep paused a stream.

#pragma once

#include "core/reducer/SleepCycle.h"

#include <cstdint>
#include <string_view>

namespace dish::reducer {

enum class LidPosition : std::uint8_t { Unknown, Open, Closed };

// GUID_LIDSWITCH_STATE_CHANGE carries a DWORD: 0 is closed, 1 is open.
inline constexpr std::uint32_t kLidSwitchClosed = 0;
inline constexpr std::uint32_t kLidSwitchOpen = 1;

inline constexpr LidPosition lidPositionFromSwitchState(std::uint32_t state) {
    if (state == kLidSwitchClosed) { return LidPosition::Closed; }
    if (state == kLidSwitchOpen) { return LidPosition::Open; }
    return LidPosition::Unknown;
}

inline constexpr std::string_view lidPositionName(LidPosition lid) {
    switch (lid) {
    case LidPosition::Unknown:
        return "unknown";
    case LidPosition::Open:
        return "open";
    case LidPosition::Closed:
        return "closed";
    }
    return "unknown";
}

enum class LidNotice : std::uint8_t { None, PausedWhileLidClosed };

struct LidSleepState {
    LidPosition lid = LidPosition::Unknown;
    bool asleep = false;
    bool streamingWhenSleepBegan = false;
    // Closed when the sleep began, or reported closed before the resume: the
    // lid notification and the suspend edge are separate messages whose order
    // is not documented.
    bool lidClosedAcrossSleep = false;

    bool operator==(const LidSleepState& o) const {
        return lid == o.lid && asleep == o.asleep &&
               streamingWhenSleepBegan == o.streamingWhenSleepBegan &&
               lidClosedAcrossSleep == o.lidClosedAcrossSleep;
    }
    bool operator!=(const LidSleepState& o) const { return !(*this == o); }
};

struct LidSleepReduction {
    LidSleepState next;
    LidNotice notice = LidNotice::None;
};

inline LidSleepReduction reduceLidChange(const LidSleepState& current, LidPosition lid) {
    const bool closedWhileAsleep = current.asleep && lid == LidPosition::Closed;
    LidSleepState next = current;
    next.lid = lid;
    next.lidClosedAcrossSleep = current.lidClosedAcrossSleep || closedWhileAsleep;
    return LidSleepReduction{next, LidNotice::None};
}

inline LidSleepReduction reduceSleepBegan(const LidSleepState& current, bool streaming) {
    const bool lidIsClosed = current.lid == LidPosition::Closed;
    const LidSleepState next{current.lid, true, streaming, lidIsClosed};
    return LidSleepReduction{next, LidNotice::None};
}

inline LidSleepReduction reduceSleepEnded(const LidSleepState& current) {
    const bool aStreamWasPaused = current.asleep && current.streamingWhenSleepBegan;
    const bool theLidPausedIt = aStreamWasPaused && current.lidClosedAcrossSleep;
    const LidNotice notice = theLidPausedIt ? LidNotice::PausedWhileLidClosed : LidNotice::None;
    const LidSleepState next{current.lid, false, false, false};
    return LidSleepReduction{next, notice};
}

// Fed the SleepCycle effect, which has already made repeated and unpaired
// edges inert. `streaming` is read before the teardown runs and only matters
// on the way down.
inline LidSleepReduction reduceLidOnSleepEffect(const LidSleepState& current, SleepEffect effect,
                                                bool streaming) {
    switch (effect) {
    case SleepEffect::None:
        return LidSleepReduction{current, LidNotice::None};
    case SleepEffect::TearDown:
        return reduceSleepBegan(current, streaming);
    case SleepEffect::Reconnect:
        return reduceSleepEnded(current);
    }
    return LidSleepReduction{current, LidNotice::None};
}

} // namespace dish::reducer
