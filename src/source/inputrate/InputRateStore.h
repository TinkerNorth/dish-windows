// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// InputRateStore — a kernel StateSource over the per-slot live input rates
// (slotId -> SlotInputRates). It periodically samples cumulative per-stream
// event counters (the gamepad report count and the motion sample count the
// input hot path exposes), runs each through a pure InputRateTracker to derive
// quantized Hz, and publishes the map. Mirrors dish-android source/inputrate/
// InputRateStore (an AbstractStateSource that samples native counters on a
// scope.launch loop).
//
// SoC: the Hz math is the pure InputRateTracker; this class owns only the
// per-slot tracker state, the counter-source seam, and the sampling cadence.
// The android low-power-hold arm is dropped (no phone power model on Windows).
//
// Testing: there are no real timers in tests. The sampling work is `sampleAt`,
// which takes the "now" instant explicitly so a test pumps the loop with a fake
// clock; `start()`/`stop()` wire a QTimer that calls `sampleAt(steady now)` in
// production only.

#pragma once

#include "architecture/StateSource.h"
#include "source/inputrate/SlotInputRates.h"
#include "core/reducer/InputRateTracker.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

class QTimer;

namespace dish::source {

// Cumulative per-slot stream counters as the hot path reports them. Monotonic
// within a device's attachment; a counter going backwards (device re-attach)
// is handled by the tracker as a rebaseline.
struct SlotInputCounters {
    std::uint64_t gamepadEvents = 0;
    std::uint64_t motionEvents = 0;
};

// slotId -> live rates. std::map keeps it deterministic + ==-comparable so the
// Observable's distinct-until-changed suppresses identical re-emits.
using SlotInputRatesMap = std::map<std::string, SlotInputRates>;

class InputRateStore : public arch::StateSource<SlotInputRatesMap> {
  public:
    // The seam the store samples on every tick: returns the current cumulative
    // counters for `slotId`, or nullopt if the hot path has no data for it
    // (treated as "no sample this tick"). Borrowed; outlives the store.
    using CounterSource = std::function<SlotInputCounters(const std::string& slotId)>;

    // Default production sampling cadence (ms). The android loop samples on a
    // similar sub-second interval; the exact value isn't wire-visible.
    static constexpr int kDefaultSampleIntervalMs = 500;

    explicit InputRateStore(CounterSource source);
    ~InputRateStore() override;

    // Register a device/slot to track. Idempotent. A freshly added slot starts
    // at all-zero rates and gets fresh trackers (its first sample establishes a
    // baseline, reporting 0 Hz).
    void addDevice(const std::string& slotId);

    // Drop a slot: removes it from the published map and discards its trackers,
    // so a later re-add re-baselines cleanly. A no-op (no emit) if absent.
    void removeDevice(const std::string& slotId);

    // Take one sample for every tracked slot at instant `nowUs` (microseconds,
    // any monotonic basis), updating current + peak Hz and republishing the map.
    // This is the test seam — production's QTimer calls it with a steady clock.
    void sampleAt(std::uint64_t nowUs);

    // Lifecycle: start() arms a repeating QTimer that calls sampleAt(now) every
    // kDefaultSampleIntervalMs; stop() cancels it. Idempotent. No-ops without a
    // Qt event loop (e.g. headless tests just call sampleAt directly).
    void start() override;
    void stop() override;

  private:
    // Per-slot pure trackers (one per stream) plus the last published rates.
    struct SlotState {
        reducer::InputRateTracker gamepad;
        reducer::InputRateTracker motion;
        SlotInputRates rates;
    };

    CounterSource source_;
    std::map<std::string, SlotState> slots_;
    std::unique_ptr<QTimer> timer_;
};

} // namespace dish::source
