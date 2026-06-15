// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ResendPacer — the pacing gate for the overlay/UDP resend loops. Real input is
// event-driven and never passes through here; resends exist solely to heal a
// LOST edge: the final frame of a gesture (button-up, finger-up, stick-to-
// neutral) that no later frame would correct. A changed state is re-sent
// kEdgeBurstResends ticks in a row, then falls back to a slow keepalive against
// pathological multi-loss. Not thread-safe — call from the single resend thread
// only. The clock is injected (a `std::function<std::int64_t()>` returning
// nanoseconds) so the schedule is unit-testable with a fake clock, exactly like
// android passes `nanoTime = { ... }`. Mirrors dish-android ui/common/ResendPacer.kt.

#pragma once

#include <cstdint>
#include <functional>
#include <utility>

namespace dish::reducer {

class ResendPacer {
  public:
    // A changed state is re-sent this many ticks in a row: one lost edge frame
    // heals at the next tick, a double loss at the one after.
    static constexpr int kEdgeBurstResends = 3;
    // After the burst, at most one keepalive per this interval (1 s in ns).
    static constexpr std::int64_t kKeepaliveIntervalNs = 1'000'000'000;

    explicit ResendPacer(std::function<std::int64_t()> nanoTime) : nanoTime_(std::move(nanoTime)) {}

    // Decide whether a resend is due on this tick. `changed` is whether the
    // assembled state differs from the last sent one. Returns true on the
    // change tick and the next (kEdgeBurstResends - 1) ticks, then only once
    // per keepalive interval. Advances the internal "last sent" clock on every
    // true.
    bool resendDue(bool changed) {
        const std::int64_t now = nanoTime_();
        if (changed) {
            burstLeft_ = kEdgeBurstResends - 1;
        } else if (burstLeft_ > 0) {
            --burstLeft_;
        } else if (now - lastSendNs_ < kKeepaliveIntervalNs) {
            return false;
        }
        lastSendNs_ = now;
        return true;
    }

  private:
    std::function<std::int64_t()> nanoTime_;
    int burstLeft_ = 0;
    std::int64_t lastSendNs_ = 0;
};

} // namespace dish::reducer
