// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pins the per-slot live-rate StateSource: device add/remove, sampling the
// injected counters through the pure tracker, current + peak Hz tracking, and a
// counter reset folding to 0. No real timers — sampleAt() is pumped with a fake
// clock and a fake counter source. Replicates dish-android source/inputrate/
// InputRateStoreTest (10 cases, low-power-hold arm dropped) + SlotInputRatesTest
// (2 cases, the hasAny predicate).

#include "source/inputrate/InputRateStore.h"
#include "source/inputrate/SlotInputRates.h"

#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <map>
#include <string>

using dish::source::InputRateStore;
using dish::source::SlotInputCounters;
using dish::source::SlotInputRates;
using dish::source::SlotInputRatesMap;
using dish::test::StateSourceProbe;

namespace {

constexpr std::uint64_t kSecondUs = 1'000'000ULL;

// A mutable bank of per-slot cumulative counters the store samples. Tests bump
// these between sampleAt() calls to simulate the hot path counting events.
struct FakeCounters {
    std::map<std::string, SlotInputCounters> counts;
    SlotInputCounters get(const std::string& slotId) const {
        const auto it = counts.find(slotId);
        if (it == counts.end()) { return SlotInputCounters{}; }
        return it->second;
    }
};

} // namespace

// ── SlotInputRates::hasAny ────────────────────────────────────────────────────

TEST_CASE("SlotInputRates::hasAny is false when all streams are zero", "[inputrate]") {
    SlotInputRates r;
    REQUIRE_FALSE(r.hasAny());
}

TEST_CASE("SlotInputRates::hasAny is true if any stream (current or peak) is set", "[inputrate]") {
    SlotInputRates a;
    a.gamepadHz = 5;
    REQUIRE(a.hasAny());

    SlotInputRates b;
    b.gamepadPeakHz = 10; // current 0 but a peak was recorded
    REQUIRE(b.hasAny());

    SlotInputRates c;
    c.motionHz = 50;
    REQUIRE(c.hasAny());

    SlotInputRates d;
    d.motionPeakHz = 250;
    REQUIRE(d.hasAny());
}

// ── Store: initial state is empty ─────────────────────────────────────────────

TEST_CASE("InputRateStore: initially publishes an empty map", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    REQUIRE(store.state().value().empty());
}

// ── Store: device add publishes a zero entry ──────────────────────────────────

TEST_CASE("InputRateStore: adding a device publishes a zero entry", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    StateSourceProbe<SlotInputRatesMap> probe(store.state());

    store.addDevice("padA");
    const auto& snap = store.state().value();
    REQUIRE(snap.count("padA") == 1);
    REQUIRE(snap.at("padA") == SlotInputRates{});
    // Eager initial (empty) + the add.
    REQUIRE(probe.count() == 2);
}

TEST_CASE("InputRateStore: adding the same device twice is idempotent", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");
    StateSourceProbe<SlotInputRatesMap> probe(store.state());
    store.addDevice("padA");     // no-op
    REQUIRE(probe.count() == 1); // only the eager initial
}

// ── Store: a single sample advances the rates ─────────────────────────────────

TEST_CASE("InputRateStore: a sample interval advances gamepad and motion Hz", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");

    // First sample anchors the baseline (reports 0).
    fake.counts["padA"] = SlotInputCounters{0, 0};
    store.sampleAt(0);
    // 100 gamepad events + 50 motion samples over one second.
    fake.counts["padA"] = SlotInputCounters{100, 50};
    store.sampleAt(kSecondUs);

    const SlotInputRates r = store.state().value().at("padA");
    REQUIRE(r.gamepadHz == 100);
    REQUIRE(r.motionHz == 50);
    REQUIRE(r.gamepadPeakHz == 100);
    REQUIRE(r.motionPeakHz == 50);
}

// ── Store: peak is retained when the rate later drops ─────────────────────────

TEST_CASE("InputRateStore: peak Hz is retained after the rate drops", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");

    fake.counts["padA"] = SlotInputCounters{0, 0};
    store.sampleAt(0);
    fake.counts["padA"] = SlotInputCounters{200, 0}; // 200 Hz burst
    store.sampleAt(kSecondUs);
    fake.counts["padA"] = SlotInputCounters{250, 0}; // only 50 more next second
    store.sampleAt(2 * kSecondUs);

    const SlotInputRates r = store.state().value().at("padA");
    REQUIRE(r.gamepadHz == 50);
    REQUIRE(r.gamepadPeakHz == 200); // peak survives the drop
}

// ── Store: device removal drops the entry ─────────────────────────────────────

TEST_CASE("InputRateStore: removing a device drops its entry", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");
    store.addDevice("padB");

    store.removeDevice("padA");
    const auto& snap = store.state().value();
    REQUIRE(snap.count("padA") == 0);
    REQUIRE(snap.count("padB") == 1);
}

TEST_CASE("InputRateStore: removing an absent device is a no-op", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");
    StateSourceProbe<SlotInputRatesMap> probe(store.state());
    store.removeDevice("ghost"); // not present
    REQUIRE(probe.count() == 1); // only the eager initial, no spurious emit
}

// ── Store: multiple devices are tracked independently ─────────────────────────

TEST_CASE("InputRateStore: tracks multiple devices independently", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");
    store.addDevice("padB");

    fake.counts["padA"] = SlotInputCounters{0, 0};
    fake.counts["padB"] = SlotInputCounters{0, 0};
    store.sampleAt(0);
    fake.counts["padA"] = SlotInputCounters{100, 0};
    fake.counts["padB"] = SlotInputCounters{30, 0};
    store.sampleAt(kSecondUs);

    const auto& snap = store.state().value();
    REQUIRE(snap.at("padA").gamepadHz == 100);
    REQUIRE(snap.at("padB").gamepadHz == 30);
}

// ── Store: a native counter reset folds the rate to 0 ─────────────────────────

TEST_CASE("InputRateStore: a counter reset yields 0 Hz that tick", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");

    fake.counts["padA"] = SlotInputCounters{0, 0};
    store.sampleAt(0);
    fake.counts["padA"] = SlotInputCounters{500, 0};
    store.sampleAt(kSecondUs);
    REQUIRE(store.state().value().at("padA").gamepadHz == 500);
    // Counter wraps back to a small value (device re-attach / native reset).
    fake.counts["padA"] = SlotInputCounters{10, 0};
    store.sampleAt(2 * kSecondUs);
    REQUIRE(store.state().value().at("padA").gamepadHz == 0);
}

// ── Store: re-adding a removed device re-baselines cleanly ────────────────────

TEST_CASE("InputRateStore: re-adding a removed device starts fresh", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    store.addDevice("padA");
    fake.counts["padA"] = SlotInputCounters{0, 0};
    store.sampleAt(0);
    fake.counts["padA"] = SlotInputCounters{100, 0};
    store.sampleAt(kSecondUs);
    REQUIRE(store.state().value().at("padA").gamepadPeakHz == 100);

    store.removeDevice("padA");
    store.addDevice("padA"); // fresh trackers + zeroed rates

    REQUIRE(store.state().value().at("padA") == SlotInputRates{});
    // The first sample after re-add anchors (reports 0) even though the counter
    // is still high — no spurious huge rate.
    fake.counts["padA"] = SlotInputCounters{1000, 0};
    store.sampleAt(2 * kSecondUs);
    REQUIRE(store.state().value().at("padA").gamepadHz == 0);
}

// ── Store: sampling with no devices is a quiet no-op ──────────────────────────

TEST_CASE("InputRateStore: sampling with no devices emits nothing", "[inputrate]") {
    FakeCounters fake;
    InputRateStore store([&](const std::string& s) { return fake.get(s); });
    StateSourceProbe<SlotInputRatesMap> probe(store.state());
    store.sampleAt(kSecondUs);
    REQUIRE(probe.count() == 1); // only the eager initial empty map
}
