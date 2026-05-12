// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QObject>
#include <QString>

namespace dish::util {

// Keeps the system display awake while Dish is streaming. The Windows analogue
// of Android's WakeStateController (PARTIAL_WAKE_LOCK + FLAG_KEEP_SCREEN_ON),
// dish-mac's IOPMAssertion, and dish-linux's QtDBus call to
// org.freedesktop.ScreenSaver. Implemented over `SetThreadExecutionState`
// with `ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED | ES_CONTINUOUS` — the
// Win32 portal every Windows desktop honours (including Modern Standby /
// "S0 low-power idle" sleep on laptops).
//
// Tests use FakeDisplaySleepInhibitor so we can pin the acquire/release
// lifecycle without flipping a real OS power-management flag in CI.
class DisplaySleepInhibitor : public QObject {
    Q_OBJECT
  public:
    explicit DisplaySleepInhibitor(QObject* parent = nullptr) : QObject(parent) {}
    ~DisplaySleepInhibitor() override = default;

    // Idempotent: a second acquire while already held is a no-op so callers
    // don't have to track state themselves.
    virtual void acquire(const QString& reason) = 0;
    // Idempotent: releasing while not held is a no-op.
    virtual void release() = 0;
    // True iff an inhibit flag is currently held.
    virtual bool isHeld() const = 0;
};

// Production implementation. SetThreadExecutionState is process-wide on
// Windows (despite the "Thread" in the name — Microsoft naming quirk from
// the NT 3.51 era). This RAII wrapper guarantees that on dealloc we hand
// the flag back even if the AppModel forgot to release.
class SetThreadExecutionStateInhibitor : public DisplaySleepInhibitor {
    Q_OBJECT
  public:
    explicit SetThreadExecutionStateInhibitor(QObject* parent = nullptr);
    ~SetThreadExecutionStateInhibitor() override;

    void acquire(const QString& reason) override;
    void release() override;
    bool isHeld() const override { return held_; }

  private:
    bool held_ = false;
};

} // namespace dish::util
