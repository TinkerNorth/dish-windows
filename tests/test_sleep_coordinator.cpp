// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Driven through a fake monitor and the effect-sink constructor, so the
// suspend edge is exercised without a power broadcast or a live satellite. The sink records rather
// than acts: what is pinned here is which effects come out, and in what order, and when the
// lid-closed resume is announced.

#include "composer/SleepCoordinator.h"
#include "core/reducer/LidSleep.h"
#include "core/reducer/SleepCycle.h"
#include "source/system/SleepMonitor.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>

#include <vector>

using dish::composer::SleepCoordinator;
using dish::reducer::LidPosition;
using dish::reducer::SleepEffect;
using dish::reducer::SleepPhase;

namespace {

// start()/stop() only record: the coordinator does not own the monitor's
// lifecycle, and a fake that acted on them would hide that.
class FakeSleepMonitor : public dish::source::SleepMonitor {
  public:
    void start() override { ++starts_; }
    void stop() override { ++stops_; }

    // The edge WM_POWERBROADCAST would deliver.
    void emitPreparingForSleep(bool starting) { emit preparingForSleep(starting); }
    // The position GUID_LIDSWITCH_STATE_CHANGE would deliver.
    void emitLid(LidPosition position) { emit lidPositionChanged(position); }

    int starts() const { return starts_; }
    int stops() const { return stops_; }

  private:
    int starts_ = 0;
    int stops_ = 0;
};

// QSignalSpy stand-in: DishTests links Catch2, not Qt6::Test.
struct NoticeSpy {
    int count = 0;
    explicit NoticeSpy(SleepCoordinator* coordinator) {
        QObject::connect(coordinator, &SleepCoordinator::pausedWhileLidClosed, [this] { ++count; });
    }
};

// A streaming flag the sink clears on teardown, the way closing the sessions
// drops the streaming count.
struct StreamingFixture {
    bool streaming = true;
    std::vector<SleepEffect> effects;

    void record(SleepEffect effect) {
        effects.push_back(effect);
        if (effect == SleepEffect::TearDown) { streaming = false; }
    }
    bool isStreaming() const { return streaming; }
};

SleepCoordinator::EffectSink sinkFor(StreamingFixture* fixture) {
    return [fixture](SleepEffect effect) { fixture->record(effect); };
}

SleepCoordinator::StreamingProbe probeFor(const StreamingFixture* fixture) {
    return [fixture] { return fixture->isStreaming(); };
}

void sleepAndWake(FakeSleepMonitor* monitor) {
    monitor->emitPreparingForSleep(true);
    monitor->emitPreparingForSleep(false);
}

} // namespace

TEST_CASE("SleepCoordinator: a suspend tears the sessions down exactly once", "[sleep]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });
    REQUIRE(effects.empty());

    monitor.emitPreparingForSleep(true);
    REQUIRE(effects.size() == 1);
    REQUIRE(effects.at(0) == SleepEffect::TearDown);
    REQUIRE(c.phase() == SleepPhase::Suspending);
}

TEST_CASE("SleepCoordinator: a repeated suspend emits nothing more", "[sleep]") {
    // Windows repeats PBT_APMSUSPEND when a suspend escalates to hibernate; a
    // second teardown would fire against sessions already gone.
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });

    monitor.emitPreparingForSleep(true);
    monitor.emitPreparingForSleep(true);
    monitor.emitPreparingForSleep(true);
    REQUIRE(effects.size() == 1);
    REQUIRE(c.phase() == SleepPhase::Suspending);
}

TEST_CASE("SleepCoordinator: a resume reconnects exactly once", "[sleep]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });

    monitor.emitPreparingForSleep(true);
    monitor.emitPreparingForSleep(false);
    REQUIRE(effects.size() == 2);
    REQUIRE(effects.at(1) == SleepEffect::Reconnect);
    REQUIRE(c.phase() == SleepPhase::Awake);

    monitor.emitPreparingForSleep(false);
    REQUIRE(effects.size() == 2);
}

TEST_CASE("SleepCoordinator: a resume with no prior suspend emits nothing", "[sleep]") {
    // Dish can start after the suspend edge, or miss it on a bus reconnect.
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });

    monitor.emitPreparingForSleep(false);
    REQUIRE(effects.empty());
    REQUIRE(c.phase() == SleepPhase::Awake);
}

TEST_CASE("SleepCoordinator: a full cycle emits teardown and reconnect in order", "[sleep]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });

    monitor.emitPreparingForSleep(true);
    monitor.emitPreparingForSleep(false);
    monitor.emitPreparingForSleep(true);
    monitor.emitPreparingForSleep(false);

    REQUIRE(effects.size() == 4);
    REQUIRE(effects.at(0) == SleepEffect::TearDown);
    REQUIRE(effects.at(1) == SleepEffect::Reconnect);
    REQUIRE(effects.at(2) == SleepEffect::TearDown);
    REQUIRE(effects.at(3) == SleepEffect::Reconnect);
    REQUIRE(c.phase() == SleepPhase::Awake);
}

TEST_CASE("SleepCoordinator: phase tracks the edge throughout", "[sleep]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });
    REQUIRE(c.phase() == SleepPhase::Awake);

    monitor.emitPreparingForSleep(true);
    REQUIRE(c.phase() == SleepPhase::Suspending);
    monitor.emitPreparingForSleep(true);
    REQUIRE(c.phase() == SleepPhase::Suspending);
    monitor.emitPreparingForSleep(false);
    REQUIRE(c.phase() == SleepPhase::Awake);
    monitor.emitPreparingForSleep(false);
    REQUIRE(c.phase() == SleepPhase::Awake);
}

TEST_CASE("SleepCoordinator: the monitor's lifecycle is left to the shell", "[sleep]") {
    // The monitor is borrowed, not owned: whoever built it starts it.
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });
    REQUIRE(monitor.starts() == 0);
    REQUIRE(monitor.stops() == 0);
    REQUIRE(c.phase() == SleepPhase::Awake);
}

TEST_CASE("SleepCoordinator: tolerates a null monitor", "[sleep]") {
    // A headless build has no edge to subscribe to; construction still
    // has to succeed, and the phase stays awake forever.
    std::vector<SleepEffect> effects;
    SleepCoordinator c(nullptr, [&effects](SleepEffect e) { effects.push_back(e); });
    REQUIRE(effects.empty());
    REQUIRE(c.phase() == SleepPhase::Awake);
}

TEST_CASE("SleepCoordinator: a lid-closed sleep that paused streaming is announced once",
          "[sleep][lid]") {
    FakeSleepMonitor monitor;
    StreamingFixture fixture;
    SleepCoordinator c(&monitor, sinkFor(&fixture), probeFor(&fixture));
    NoticeSpy spy(&c);

    monitor.emitLid(LidPosition::Closed);
    monitor.emitPreparingForSleep(true);
    REQUIRE(spy.count == 0);
    monitor.emitLid(LidPosition::Open);
    monitor.emitPreparingForSleep(false);
    REQUIRE(spy.count == 1);
    // The second resume event of the same wake is inert.
    monitor.emitPreparingForSleep(false);
    REQUIRE(spy.count == 1);
    REQUIRE(fixture.effects ==
            std::vector<SleepEffect>{SleepEffect::TearDown, SleepEffect::Reconnect});
}

TEST_CASE("SleepCoordinator: streaming is read before the teardown closes it", "[sleep][lid]") {
    // The sink drops the streaming flag on teardown; reading the probe after it
    // would never announce anything.
    FakeSleepMonitor monitor;
    StreamingFixture fixture;
    SleepCoordinator c(&monitor, sinkFor(&fixture), probeFor(&fixture));
    NoticeSpy spy(&c);

    monitor.emitLid(LidPosition::Closed);
    sleepAndWake(&monitor);
    REQUIRE_FALSE(fixture.streaming);
    REQUIRE(spy.count == 1);
}

TEST_CASE("SleepCoordinator: a lid-closed sleep with nothing streaming says nothing",
          "[sleep][lid]") {
    FakeSleepMonitor monitor;
    StreamingFixture fixture;
    fixture.streaming = false;
    SleepCoordinator c(&monitor, sinkFor(&fixture), probeFor(&fixture));
    NoticeSpy spy(&c);

    monitor.emitLid(LidPosition::Closed);
    sleepAndWake(&monitor);
    REQUIRE(spy.count == 0);
}

TEST_CASE("SleepCoordinator: a sleep with the lid open says nothing", "[sleep][lid]") {
    FakeSleepMonitor monitor;
    StreamingFixture fixture;
    SleepCoordinator c(&monitor, sinkFor(&fixture), probeFor(&fixture));
    NoticeSpy spy(&c);

    monitor.emitLid(LidPosition::Open);
    sleepAndWake(&monitor);
    REQUIRE(spy.count == 0);
}

TEST_CASE("SleepCoordinator: without a streaming probe nothing is announced", "[sleep][lid]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });
    NoticeSpy spy(&c);

    monitor.emitLid(LidPosition::Closed);
    sleepAndWake(&monitor);
    REQUIRE(spy.count == 0);
    REQUIRE(effects.size() == 2);
}

TEST_CASE("SleepCoordinator: the lid position tracks the monitor", "[sleep][lid]") {
    FakeSleepMonitor monitor;
    std::vector<SleepEffect> effects;
    SleepCoordinator c(&monitor, [&effects](SleepEffect e) { effects.push_back(e); });
    REQUIRE(c.lid() == LidPosition::Unknown);
    monitor.emitLid(LidPosition::Closed);
    REQUIRE(c.lid() == LidPosition::Closed);
    monitor.emitLid(LidPosition::Open);
    REQUIRE(c.lid() == LidPosition::Open);
    REQUIRE(effects.empty());
}
