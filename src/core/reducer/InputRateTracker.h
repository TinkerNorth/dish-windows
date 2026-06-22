// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// InputRateTracker — the pure per-stream rate estimator behind the live-stats
// display. Given a monotonically-increasing event counter sampled at arbitrary
// instants, it derives a smoothed events-per-second value, quantized to 5 Hz
// steps so a jittery counter doesn't make the readout flicker by ±1 Hz every
// frame. Mirrors dish-android source/inputrate/InputRateTracker (a pure helper):
// event-count delta -> Hz, 5 Hz quantization, counter-reset -> 0, rebaseline.
//
// Pure C++17, Qt-free, no allocation: the InputRateStore owns one of these per
// stream and feeds it the native counter on its sampling tick; the math here is
// unit-testable in isolation. There is no clock inside — the caller passes the
// sample instant (microseconds, any monotonic basis), mirroring the hot-path
// rule that timing comes from the caller, never from a hidden now().

#pragma once

#include <cmath>
#include <cstdint>

namespace dish::reducer {

// Quantize a raw Hz value to the nearest multiple of `step` (default 5). Round
// half away from zero so 22.5 -> 25 deterministically (std::lround is banker's-
// rounding-free). Negative input clamps to 0 (a rate is never negative). Pure.
inline int quantizeHz(double hz, int step = 5) {
    if (hz <= 0.0) { return 0; }
    if (step <= 1) { return static_cast<int>(std::lround(hz)); }
    const long stepsAway = std::lround(hz / static_cast<double>(step));
    return static_cast<int>(stepsAway) * step;
}

// A single stream's rate estimator. Holds the last (count, instant) baseline;
// sample() returns the quantized Hz over the interval since the previous sample.
class InputRateTracker {
  public:
    // Step the estimator with the absolute counter value at `nowUs`.
    //   * First ever sample (no baseline): establish the baseline, report 0 Hz.
    //   * counter went backwards (native counter reset / device re-attached):
    //     rebaseline to the new value and report 0 Hz — we never report a
    //     spurious huge rate from a wrap.
    //   * elapsed <= 0 (same or out-of-order instant): hold the last reported
    //     rate, don't divide by zero.
    //   * otherwise: Hz = deltaCount / deltaSeconds, quantized to 5 Hz.
    int sample(std::uint64_t count, std::uint64_t nowUs) {
        if (!hasBaseline_) {
            hasBaseline_ = true;
            lastCount_ = count;
            lastUs_ = nowUs;
            lastHz_ = 0;
            return 0;
        }
        if (count < lastCount_) {
            // Counter reset / rebaseline: forget the old anchor, report 0.
            lastCount_ = count;
            lastUs_ = nowUs;
            lastHz_ = 0;
            return 0;
        }
        if (nowUs <= lastUs_) {
            // No time elapsed (or clock went backwards): keep the prior reading,
            // but still advance the count anchor so the next real interval is
            // measured from here.
            lastCount_ = count;
            return lastHz_;
        }
        const std::uint64_t deltaCount = count - lastCount_;
        const std::uint64_t deltaUs = nowUs - lastUs_;
        const double hz =
            static_cast<double>(deltaCount) * 1'000'000.0 / static_cast<double>(deltaUs);
        lastCount_ = count;
        lastUs_ = nowUs;
        lastHz_ = quantizeHz(hz);
        return lastHz_;
    }

    // The most recent quantized Hz reading (0 before the first interval).
    int lastHz() const { return lastHz_; }

    // Drop the baseline so the next sample() re-anchors and reports 0. Used when
    // a device detaches and a new one might reuse the slot id.
    void reset() {
        hasBaseline_ = false;
        lastCount_ = 0;
        lastUs_ = 0;
        lastHz_ = 0;
    }

  private:
    bool hasBaseline_ = false;
    std::uint64_t lastCount_ = 0;
    std::uint64_t lastUs_ = 0;
    int lastHz_ = 0;
};

} // namespace dish::reducer
