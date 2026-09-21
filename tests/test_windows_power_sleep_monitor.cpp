// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The power-broadcast filter is driven with synthetic MSGs, so the mapping
// from Windows' power events to the suspend/resume edge is pinned without a
// window, a real suspend or the shell.

#include "source/system/SleepMonitor.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QObject>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

using dish::source::WindowsPowerSleepMonitor;

namespace {

// QSignalSpy stand-in: DishTests links Catch2, not Qt6::Test.
struct EdgeSpy {
    std::vector<bool> edges;
    explicit EdgeSpy(WindowsPowerSleepMonitor* monitor) {
        QObject::connect(monitor, &WindowsPowerSleepMonitor::preparingForSleep,
                         [this](bool starting) { edges.push_back(starting); });
    }
};

MSG powerBroadcast(WPARAM powerEvent) {
    MSG msg{};
    msg.message = WM_POWERBROADCAST;
    msg.wParam = powerEvent;
    return msg;
}

const QByteArray kWindowsEvent = QByteArrayLiteral("windows_generic_MSG");

} // namespace

TEST_CASE("power edge: a suspend is the going-down edge", "[sleep][windows]") {
    const auto edge = WindowsPowerSleepMonitor::edgeFor(PBT_APMSUSPEND);
    REQUIRE(edge.has_value());
    REQUIRE(*edge);
}

TEST_CASE("power edge: both resume events are the coming-back edge", "[sleep][windows]") {
    // PBT_APMRESUMEAUTOMATIC always arrives; PBT_APMRESUMESUSPEND follows it
    // when a user woke the machine. Mapping both keeps a resume from being
    // missed, and the SleepCycle reducer makes the repeat inert.
    const auto automatic = WindowsPowerSleepMonitor::edgeFor(PBT_APMRESUMEAUTOMATIC);
    const auto byUser = WindowsPowerSleepMonitor::edgeFor(PBT_APMRESUMESUSPEND);
    REQUIRE(automatic.has_value());
    REQUIRE_FALSE(*automatic);
    REQUIRE(byUser.has_value());
    REQUIRE_FALSE(*byUser);
}

TEST_CASE("power edge: battery and power-source changes are not an edge", "[sleep][windows]") {
    REQUIRE_FALSE(WindowsPowerSleepMonitor::edgeFor(PBT_APMPOWERSTATUSCHANGE).has_value());
    REQUIRE_FALSE(WindowsPowerSleepMonitor::edgeFor(PBT_POWERSETTINGCHANGE).has_value());
    REQUIRE_FALSE(WindowsPowerSleepMonitor::edgeFor(0).has_value());
}

TEST_CASE("power filter: a suspend broadcast emits the edge and is never consumed",
          "[sleep][windows]") {
    WindowsPowerSleepMonitor monitor;
    EdgeSpy spy(&monitor);
    MSG msg = powerBroadcast(PBT_APMSUSPEND);
    qintptr result = 0;
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &msg, &result));
    REQUIRE(spy.edges == std::vector<bool>{true});

    MSG resume = powerBroadcast(PBT_APMRESUMEAUTOMATIC);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &resume, &result));
    REQUIRE(spy.edges == std::vector<bool>{true, false});
}

TEST_CASE("power filter: other messages and other event types emit nothing", "[sleep][windows]") {
    WindowsPowerSleepMonitor monitor;
    EdgeSpy spy(&monitor);
    qintptr result = 0;

    MSG status = powerBroadcast(PBT_APMPOWERSTATUSCHANGE);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &status, &result));

    MSG other{};
    other.message = WM_TIMER;
    other.wParam = PBT_APMSUSPEND;
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &other, &result));

    MSG suspend = powerBroadcast(PBT_APMSUSPEND);
    REQUIRE_FALSE(
        monitor.nativeEventFilter(QByteArrayLiteral("xcb_generic_event_t"), &suspend, &result));
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, nullptr, &result));

    REQUIRE(spy.edges.empty());
}

TEST_CASE("power filter: start installs once and stop removes", "[sleep][windows]") {
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    if (QCoreApplication::instance() == nullptr) { static QCoreApplication app(argc, argv); }

    WindowsPowerSleepMonitor monitor;
    REQUIRE_FALSE(monitor.isStarted());
    monitor.start();
    REQUIRE(monitor.isStarted());
    monitor.start();
    REQUIRE(monitor.isStarted());
    monitor.stop();
    REQUIRE_FALSE(monitor.isStarted());
    monitor.stop();
    REQUIRE_FALSE(monitor.isStarted());
}
