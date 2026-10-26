// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The power-broadcast filter is driven with synthetic MSGs, so the mapping
// from Windows' power events to the suspend/resume edge is pinned without a
// window, a real suspend or the shell. The lid registration itself needs a
// laptop's power manager, so what is pinned for it is the decode and the
// release; whether Windows accepts the registration is logged, not asserted.

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

#include <array>
#include <cstddef>
#include <cstring>
#include <vector>

using dish::reducer::LidPosition;
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

// GUID_LIDSWITCH_STATE_CHANGE as Microsoft documents it.
constexpr GUID kLidSwitch = {
    0xba3e0f4d, 0xb817, 0x4094, {0xa2, 0xd1, 0xd5, 0x63, 0x79, 0xe6, 0xa0, 0xf3}};
// GUID_CONSOLE_DISPLAY_STATE: a power setting that is not the lid.
constexpr GUID kDisplayState = {
    0x6fe69556, 0x704a, 0x47a0, {0x8f, 0x24, 0xc2, 0x8d, 0x93, 0x6f, 0xda, 0x47}};

constexpr DWORD kLidClosed = 0;
constexpr DWORD kLidOpen = 1;

// POWERBROADCAST_SETTING ends in a one-byte Data[1]; the DWORD a lid change
// carries runs past it, so the buffer is sized for both, the way Windows
// allocates it.
struct PowerSetting {
    alignas(POWERBROADCAST_SETTING)
        std::array<unsigned char, sizeof(POWERBROADCAST_SETTING) + sizeof(DWORD)> bytes{};

    PowerSetting(const GUID& guid, DWORD state, DWORD length = static_cast<DWORD>(sizeof(DWORD))) {
        POWERBROADCAST_SETTING header{};
        header.PowerSetting = guid;
        header.DataLength = length;
        std::memcpy(bytes.data(), &header, sizeof(header));
        std::memcpy(bytes.data() + offsetof(POWERBROADCAST_SETTING, Data), &state, sizeof(state));
    }

    const void* data() const { return bytes.data(); }
    LPARAM lParam() const { return reinterpret_cast<LPARAM>(bytes.data()); }
};

struct LidSpy {
    std::vector<LidPosition> positions;
    explicit LidSpy(WindowsPowerSleepMonitor* monitor) {
        QObject::connect(monitor, &WindowsPowerSleepMonitor::lidPositionChanged,
                         [this](LidPosition position) { positions.push_back(position); });
    }
};

MSG settingChange(const PowerSetting& setting) {
    MSG msg = powerBroadcast(PBT_POWERSETTINGCHANGE);
    msg.lParam = setting.lParam();
    return msg;
}

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

TEST_CASE("lid setting: the lid switch's DWORD decodes to closed and open",
          "[sleep][lid][windows]") {
    const PowerSetting closed(kLidSwitch, kLidClosed);
    const PowerSetting open(kLidSwitch, kLidOpen);
    REQUIRE(WindowsPowerSleepMonitor::lidPositionFor(closed.data()) == LidPosition::Closed);
    REQUIRE(WindowsPowerSleepMonitor::lidPositionFor(open.data()) == LidPosition::Open);
}

TEST_CASE("lid setting: another power setting, a bad length or no setting is not the lid",
          "[sleep][lid][windows]") {
    const PowerSetting display(kDisplayState, kLidClosed);
    const PowerSetting shortPayload(kLidSwitch, kLidClosed, 1);
    REQUIRE_FALSE(WindowsPowerSleepMonitor::lidPositionFor(display.data()).has_value());
    REQUIRE_FALSE(WindowsPowerSleepMonitor::lidPositionFor(shortPayload.data()).has_value());
    REQUIRE_FALSE(WindowsPowerSleepMonitor::lidPositionFor(nullptr).has_value());
}

TEST_CASE("lid filter: a lid change emits the position, never a sleep edge, and is not consumed",
          "[sleep][lid][windows]") {
    WindowsPowerSleepMonitor monitor;
    EdgeSpy edges(&monitor);
    LidSpy lid(&monitor);
    qintptr result = 0;

    const PowerSetting closed(kLidSwitch, kLidClosed);
    MSG closing = settingChange(closed);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &closing, &result));

    const PowerSetting open(kLidSwitch, kLidOpen);
    MSG opening = settingChange(open);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &opening, &result));

    REQUIRE(lid.positions == std::vector<LidPosition>{LidPosition::Closed, LidPosition::Open});
    REQUIRE(edges.edges.empty());
}

TEST_CASE("lid filter: a setting change that is not the lid, or carries nothing, emits nothing",
          "[sleep][lid][windows]") {
    WindowsPowerSleepMonitor monitor;
    LidSpy lid(&monitor);
    qintptr result = 0;

    const PowerSetting display(kDisplayState, kLidClosed);
    MSG other = settingChange(display);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &other, &result));

    MSG empty = powerBroadcast(PBT_POWERSETTINGCHANGE);
    REQUIRE_FALSE(monitor.nativeEventFilter(kWindowsEvent, &empty, &result));

    REQUIRE(lid.positions.empty());
}

TEST_CASE("lid window: the window's broadcast takes the same path as the filter's",
          "[sleep][lid][windows]") {
    WindowsPowerSleepMonitor monitor;
    EdgeSpy edges(&monitor);
    LidSpy lid(&monitor);

    const PowerSetting closed(kLidSwitch, kLidClosed);
    monitor.onPowerBroadcast(PBT_POWERSETTINGCHANGE, closed.lParam());
    monitor.onPowerBroadcast(PBT_APMSUSPEND, 0);

    REQUIRE(lid.positions == std::vector<LidPosition>{LidPosition::Closed});
    REQUIRE(edges.edges == std::vector<bool>{true});
}

TEST_CASE("lid window: stop releases the lid registration", "[sleep][lid][windows]") {
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    if (QCoreApplication::instance() == nullptr) { static QCoreApplication app(argc, argv); }

    WindowsPowerSleepMonitor monitor;
    REQUIRE_FALSE(monitor.isWatchingTheLid());
    monitor.start();
    monitor.stop();
    REQUIRE_FALSE(monitor.isWatchingTheLid());
}
