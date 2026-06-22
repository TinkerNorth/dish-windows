// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PollRateSamplerTest (ADAPT, 6). Port of dish-android source/usb/
// PollRateSamplerTest.kt. Android mocks the JNI URB counter and a registry; here
// the pure sampler is clock- and count-injected (it returns rate updates rather
// than writing a registry), so the fake is a count source + an applied-rate map.
// Pins: Hz from the URB-count delta over the window, first-sample-only-snapshots,
// idle -> 0 (not a frozen reading), counter-reset -> 0 (never negative), detach
// finality (a removed device is not resurrected by a later sample), and
// re-attach-fresh (the same id after detach restarts from a fresh snapshot).

#include "core/reducer/PollRateSampler.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <optional>
#include <vector>

using dish::reducer::PollRateSampler;

namespace {

constexpr int kDeviceId = -1000;

// A tiny stand-in for the registry: the device's current applied poll rate, set
// only when the sampler emits an update for it. nullopt == "device not present"
// (the detach case).
struct FakeRegistry {
    std::map<int, int> rates; // present device -> last applied rate.
    std::map<int, std::int64_t> counts;
    std::vector<int> presentIds() const {
        std::vector<int> ids;
        for (const auto& [id, _] : rates) { ids.push_back(id); }
        return ids;
    }
    void seed(int id) { rates[id] = 0; }
    void remove(int id) { rates.erase(id); }
    std::optional<int> rateOf(int id) const {
        const auto it = rates.find(id);
        if (it == rates.end()) { return std::nullopt; }
        return it->second;
    }
};

// Run one sample tick and fold the returned updates back into the registry.
void sampleAll(PollRateSampler& sampler, FakeRegistry& reg, std::int64_t nowMs) {
    const auto updates =
        sampler.sampleAll(nowMs, reg.presentIds(), [&](int id) { return reg.counts[id]; });
    for (const auto& u : updates) {
        if (reg.rates.find(u.deviceId) != reg.rates.end()) { reg.rates[u.deviceId] = u.rateHz; }
    }
}

} // namespace

TEST_CASE("derives Hz from the URB-count delta over the sampling window", "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 0;
    sampleAll(sampler, reg, 1000);
    reg.counts[kDeviceId] = 500;
    sampleAll(sampler, reg, 1500);
    CHECK(reg.rateOf(kDeviceId) == 1000);
}

TEST_CASE("first sample only snapshots and does not write a rate", "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 500;
    sampleAll(sampler, reg, 1000);
    CHECK(reg.rateOf(kDeviceId) == 0);
}

TEST_CASE("an idle controller whose count stops moving reports zero rather than freezing",
          "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 0;
    sampleAll(sampler, reg, 1000);
    reg.counts[kDeviceId] = 500;
    sampleAll(sampler, reg, 1500);
    CHECK(reg.rateOf(kDeviceId) == 1000);
    // No new completions in the next window -> 0, not the frozen 1000.
    sampleAll(sampler, reg, 2000);
    CHECK(reg.rateOf(kDeviceId) == 0);
}

TEST_CASE("a counter reset cannot produce a negative rate", "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 1000;
    sampleAll(sampler, reg, 1000);
    reg.counts[kDeviceId] = 5;
    sampleAll(sampler, reg, 1500);
    CHECK(reg.rateOf(kDeviceId) == 0);
}

TEST_CASE("a detached device is not resurrected by a later sample", "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 100;
    sampleAll(sampler, reg, 1000);
    reg.remove(kDeviceId); // detach: drops out of `present`.
    reg.counts[kDeviceId] = 600;
    sampleAll(sampler, reg, 1500);
    CHECK_FALSE(reg.rateOf(kDeviceId).has_value());
}

TEST_CASE("re-attaching the same id after detach starts from a fresh snapshot", "[poll-sampler]") {
    PollRateSampler sampler;
    FakeRegistry reg;
    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 5000;
    sampleAll(sampler, reg, 1000);
    reg.remove(kDeviceId);
    sampleAll(sampler, reg, 1500); // device absent: snapshot retired.

    reg.seed(kDeviceId);
    reg.counts[kDeviceId] = 10;
    sampleAll(sampler, reg, 2000); // first sample of the re-attached id: snapshot only.
    CHECK(reg.rateOf(kDeviceId) == 0);
    reg.counts[kDeviceId] = 510;
    sampleAll(sampler, reg, 2500);
    CHECK(reg.rateOf(kDeviceId) == 1000);
}
