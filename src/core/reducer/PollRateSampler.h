// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PollRateSampler — the pure URB/transfer-completion-count delta sampler that
// turns a running per-device completion counter into a measured poll rate (Hz).
// Port of the testable core of dish-android source/usb/PollRateSampler.kt.
//
// On android the class is a coroutine that, every 500 ms, reads each synthetic
// device's URB count from JNI and writes the measured rate back into the
// registry. The COROUTINE/IO half is platform glue (it lives in the Windows
// driver, source/usb/UsbGamepadManager); the DECISION half — keep a
// {count, elapsedMs} snapshot per device, compute measuredPollRateHz over the
// window, drop snapshots for devices no longer present — is pure and is what
// the PollRateSamplerTest pins (6 cases). So this header is that pure half,
// Qt-free and clock-injected:
//
//   sampleAll(nowMs, present, countOf) -> std::vector<RateUpdate>
//
// where `present` is the set of currently-tracked device ids and `countOf(id)`
// returns that device's current completion count. A device's FIRST sample only
// snapshots (no update emitted); subsequent samples emit measuredPollRateHz
// (idle -> 0, counter-reset -> 0). A device that drops out of `present` has its
// snapshot retired, so re-attaching the same id later starts from a fresh
// snapshot rather than a stale baseline (the detach-finality + re-attach-fresh
// cases). This mirrors PollRateSampler.sampleAll's `snapshots.keys.retainAll`.

#pragma once

#include "core/reducer/UsbPollRate.h"

#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace dish::reducer {

class PollRateSampler {
  public:
    // One device's freshly-measured rate, ready for the driver to write into the
    // registry / live-stats source.
    struct RateUpdate {
        int deviceId = 0;
        int rateHz = 0;
    };

    // Take one sample over every present device. `present` is the set of device
    // ids tracked right now (only these survive in the snapshot map afterwards);
    // `countOf` reads the current completion counter for a device. Returns the
    // rate updates for the devices that had a prior snapshot to diff against
    // (a device's first sample only seeds the baseline and emits nothing).
    std::vector<RateUpdate> sampleAll(std::int64_t nowMs, const std::vector<int>& present,
                                      const std::function<std::int64_t(int)>& countOf) {
        std::vector<RateUpdate> updates;
        for (int id : present) {
            const std::int64_t count = countOf(id);
            const auto it = snapshots_.find(id);
            const bool hadPrev = it != snapshots_.end();
            Snapshot prev{};
            if (hadPrev) { prev = it->second; }
            snapshots_[id] = Snapshot{count, nowMs};
            if (!hadPrev) { continue; }
            updates.push_back(
                RateUpdate{id, measuredPollRateHz(count - prev.count, nowMs - prev.elapsedMs)});
        }
        // Retire snapshots for devices that are no longer present, so a later
        // re-attach of the same id starts fresh (matches retainAll(present)).
        retainOnly(present);
        return updates;
    }

    // Drop a single device's snapshot (e.g. an explicit detach between sample
    // ticks). A no-op if it was never sampled.
    void forget(int deviceId) { snapshots_.erase(deviceId); }

    void reset() { snapshots_.clear(); }

  private:
    struct Snapshot {
        std::int64_t count = 0;
        std::int64_t elapsedMs = 0;
    };

    void retainOnly(const std::vector<int>& present) {
        for (auto it = snapshots_.begin(); it != snapshots_.end();) {
            bool keep = false;
            for (int id : present) {
                if (id == it->first) {
                    keep = true;
                    break;
                }
            }
            if (keep) {
                ++it;
            } else {
                it = snapshots_.erase(it);
            }
        }
    }

    std::unordered_map<int, Snapshot> snapshots_;
};

} // namespace dish::reducer
