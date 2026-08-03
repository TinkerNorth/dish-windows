// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QObject>
#include <QString>

namespace dish::util {

// Keeps the display and the system awake while streaming. Abstract so tests
// can pin the acquire/release lifecycle without flipping a real OS
// power-management flag.
class DisplaySleepInhibitor : public QObject {
    Q_OBJECT
  public:
    explicit DisplaySleepInhibitor(QObject* parent = nullptr) : QObject(parent) {}
    ~DisplaySleepInhibitor() override = default;

    // Both idempotent: callers need not track held state themselves.
    virtual void acquire(const QString& reason) = 0;
    virtual void release() = 0;
    virtual bool isHeld() const = 0;
};

// SetThreadExecutionState is process-wide despite the "Thread" in the name.
// Releases on destruction so a missed release() can't pin the display awake.
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
