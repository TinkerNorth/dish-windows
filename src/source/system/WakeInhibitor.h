// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Holds the machine — and optionally the display — awake while Dish streams.
// A Gateway: one SetThreadExecutionState assertion and nothing else.

#pragma once

#include "core/reducer/KeepAwake.h"

#include <QObject>
#include <QString>

namespace dish::source {

// Abstract so tests can pin the lifecycle without flipping a real OS
// power-management flag.
class WakeInhibitor : public QObject {
    Q_OBJECT
  public:
    explicit WakeInhibitor(QObject* parent = nullptr) : QObject(parent) {}
    ~WakeInhibitor() override = default;

    // Idempotent and absolute: callers pass the reach they want, not a delta.
    virtual void apply(reducer::KeepAwakeReach reach, const QString& reason) = 0;
    virtual reducer::KeepAwakeReach held() const = 0;
};

// Releases on destruction so a missed apply() cannot pin the machine awake.
class SetThreadExecutionStateInhibitor : public WakeInhibitor {
    Q_OBJECT
  public:
    explicit SetThreadExecutionStateInhibitor(QObject* parent = nullptr);
    ~SetThreadExecutionStateInhibitor() override;

    void apply(reducer::KeepAwakeReach reach, const QString& reason) override;
    reducer::KeepAwakeReach held() const override { return held_; }

  private:
    reducer::KeepAwakeReach held_ = reducer::KeepAwakeReach::None;
};

} // namespace dish::source
