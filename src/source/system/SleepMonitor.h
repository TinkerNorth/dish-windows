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

#pragma once

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
};

class WindowsPowerSleepMonitor final : public SleepMonitor, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit WindowsPowerSleepMonitor(QObject* parent = nullptr);
    ~WindowsPowerSleepMonitor() override;

    // Installs / removes the filter on the application. Idempotent.
    void start() override;
    void stop() override;

    // Never consumes the message: every window still sees its own broadcast.
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

    // The edge a WM_POWERBROADCAST wParam means: true for PBT_APMSUSPEND, false
    // for the two resume events, nothing for everything else (battery and
    // power-source changes ride the same message). Pure, so the mapping is
    // pinned without a window.
    static std::optional<bool> edgeFor(unsigned long long powerEvent);

    bool isStarted() const { return started_; }

  private:
    bool started_ = false;
};

} // namespace dish::source
