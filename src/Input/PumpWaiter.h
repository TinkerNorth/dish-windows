// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/input/PumpPacing.h"

namespace dish::input {

// Waits for the pump's next deadline: on a high-resolution waitable timer, or by spinning on one
// core when the policy asks for it. Owned and used by the SDL thread alone.
class PumpWaiter {
  public:
    explicit PumpWaiter(pump::WaitStrategy strategy);
    ~PumpWaiter();

    PumpWaiter(const PumpWaiter&) = delete;
    PumpWaiter& operator=(const PumpWaiter&) = delete;
    PumpWaiter(PumpWaiter&&) = delete;
    PumpWaiter& operator=(PumpWaiter&&) = delete;

    // Returns at or after `deadline`, at once when it has passed.
    void waitUntil(pump::Clock::time_point deadline) const;

    // False when Windows refused CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, so a timed wait sleeps a
    // scheduler tick instead.
    bool highResolution() const { return timer_ != nullptr; }

  private:
    const pump::WaitStrategy strategy_;
    void* const timer_;
};

} // namespace dish::input
