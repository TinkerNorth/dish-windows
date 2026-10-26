// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The suspend/resume edge. Abstract so the coordinator can be driven by a fake
// without a power event; the Windows implementation below listens for
// WM_POWERBROADCAST.
//
// Windows delivers PBT_APMSUSPEND to every top-level window right before the
// machine goes down (a hidden window still gets it, which is what lets a Dish
// closed to the tray hear it) and gives the process roughly two seconds to act
// on it. On the way back PBT_APMRESUMEAUTOMATIC always arrives, and
// PBT_APMRESUMESUSPEND follows it when a user woke the machine; both read as
// the same resume here, and the reducer (core/reducer/SleepCycle.h) makes the
// repeat inert. There is no delay lock to hold, unlike logind: the message is
// delivered synchronously and the sessions are closed inside the handler.
//
// The lid rides the same message as PBT_POWERSETTINGCHANGE, but only to a
// window that registered for GUID_LIDSWITCH_STATE_CHANGE, so the monitor owns
// a message-only window for that registration; Windows sends the current
// position as soon as it is made. Knowing the lid changes nothing about the
// sleep itself: a lid close clears every power request Dish holds.

#pragma once

#include "core/reducer/LidSleep.h"

#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <QObject>

#include <optional>

namespace dish::source {

class SleepMonitor : public QObject {
    Q_OBJECT
  public:
    explicit SleepMonitor(QObject* parent = nullptr) : QObject(parent) {}
    ~SleepMonitor() override = default;

    virtual void start() = 0;
    virtual void stop() = 0;

  signals:
    void preparingForSleep(bool starting);
    void lidPositionChanged(dish::reducer::LidPosition position);
};

class WindowsPowerSleepMonitor final : public SleepMonitor, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit WindowsPowerSleepMonitor(QObject* parent = nullptr);
    ~WindowsPowerSleepMonitor() override;

    // Installs / removes the filter on the application and the lid
    // registration. Idempotent.
    void start() override;
    void stop() override;

    // Never consumes the message: every window still sees its own broadcast.
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // Both doors land here: the filter (broadcasts to Qt's own windows) and
    // the lid window. `data` is the message's lParam.
    void onPowerBroadcast(quintptr powerEvent, qintptr data);

    // The edge a WM_POWERBROADCAST wParam means: true for PBT_APMSUSPEND, false
    // for the two resume events, nothing for everything else (battery and
    // power-source changes ride the same message). Pure, so the mapping is
    // pinned without a window.
    static std::optional<bool> edgeFor(unsigned long long powerEvent);

    // The lid position a PBT_POWERSETTINGCHANGE's POWERBROADCAST_SETTING
    // carries, or nothing when the setting is another one or malformed.
    static std::optional<reducer::LidPosition> lidPositionFor(const void* powerSetting);

    bool isStarted() const { return started_; }
    bool isWatchingTheLid() const { return lidNotification_ != nullptr; }

  private:
    void watchTheLid();
    void stopWatchingTheLid();

    bool started_ = false;
    // An HWND and an HPOWERNOTIFY, kept opaque so this header stays free of
    // <windows.h>.
    void* lidWindow_ = nullptr;
    void* lidNotification_ = nullptr;
};

} // namespace dish::source
