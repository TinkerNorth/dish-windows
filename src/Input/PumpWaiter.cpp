// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Input/PumpWaiter.h"

#include "core/thread/TrackedMutex.h"

#include <windows.h>

#include <chrono>

namespace dish::input {

namespace {

// Windows 10 1803 and later; an older system refuses the flag and gets the plain sleep.
void* createHighResolutionTimer(pump::WaitStrategy strategy) {
    if (strategy != pump::WaitStrategy::Timer) { return nullptr; }
    return ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                    TIMER_ALL_ACCESS);
}

} // namespace

PumpWaiter::PumpWaiter(pump::WaitStrategy strategy)
    : strategy_(strategy), timer_(createHighResolutionTimer(strategy)) {}

PumpWaiter::~PumpWaiter() {
    if (timer_ != nullptr) { ::CloseHandle(timer_); }
}

void PumpWaiter::waitUntil(pump::Clock::time_point deadline) const {
    if (strategy_ == pump::WaitStrategy::Spin) {
        while (pump::Clock::now() < deadline) { YieldProcessor(); }
        return;
    }
    const auto remaining = deadline - pump::Clock::now();
    if (remaining <= pump::Clock::duration::zero()) { return; }
    thread::blockingRegion("pump.wait");
    if (timer_ == nullptr) {
        ::Sleep(1);
        return;
    }
    // A negative due time is relative, in 100 ns units.
    LARGE_INTEGER due{};
    due.QuadPart = -std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count() / 100;
    if (::SetWaitableTimerEx(timer_, &due, 0, nullptr, nullptr, nullptr, 0) == 0) {
        ::Sleep(1);
        return;
    }
    ::WaitForSingleObject(timer_, INFINITE);
}

} // namespace dish::input
