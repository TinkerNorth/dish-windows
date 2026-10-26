// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Turns the machine's suspend edge into connection commands, and remembers the
// lid across it so a stream that a lid-closed sleep paused is announced once
// after the resume. The effect sink and the streaming probe are seams so the
// reduction can be pinned without a power event or a live satellite.

#pragma once

#include "core/reducer/LidSleep.h"
#include "core/reducer/SleepCycle.h"

#include <QObject>

#include <functional>

namespace dish::source {
class SleepMonitor;
}

namespace dish::composer {

class ConnectionCoordinator;

class SleepCoordinator : public QObject {
    Q_OBJECT
  public:
    using EffectSink = std::function<void(reducer::SleepEffect)>;
    // Whether a slot is streaming, read as the sleep begins and before the
    // teardown closes it. An empty probe reads as nothing streaming.
    using StreamingProbe = std::function<bool()>;

    SleepCoordinator(source::SleepMonitor* monitor, ConnectionCoordinator* connections,
                     StreamingProbe isStreaming, QObject* parent = nullptr);
    SleepCoordinator(source::SleepMonitor* monitor, EffectSink sink,
                     StreamingProbe isStreaming = {}, QObject* parent = nullptr);
    ~SleepCoordinator() override = default;

    reducer::SleepPhase phase() const { return phase_; }
    reducer::LidPosition lid() const { return lid_.lid; }

  signals:
    // Once per resume, after a sleep that began (or went on) with the lid
    // closed while a slot was streaming.
    void pausedWhileLidClosed();

  private:
    void onPreparingForSleep(bool starting);
    void onLidPositionChanged(reducer::LidPosition position);
    void announce(reducer::LidNotice notice);

    EffectSink sink_;
    StreamingProbe isStreaming_;
    reducer::SleepPhase phase_ = reducer::SleepPhase::Awake;
    reducer::LidSleepState lid_;
};

} // namespace dish::composer
