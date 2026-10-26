// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/SleepCoordinator.h"

#include "composer/ConnectionCoordinator.h"
#include "source/system/SleepMonitor.h"

#include <QLoggingCategory>

#include <utility>

namespace dish::composer {

namespace {

Q_LOGGING_CATEGORY(lcDishSleep, "dish.sleep")

void applyToConnections(ConnectionCoordinator* connections, reducer::SleepEffect effect) {
    if (connections == nullptr) { return; }
    switch (effect) {
    case reducer::SleepEffect::None:
        break;
    case reducer::SleepEffect::TearDown:
        connections->prepareForSleep();
        break;
    case reducer::SleepEffect::Reconnect:
        connections->resumeFromSleep();
        break;
    }
}

} // namespace

SleepCoordinator::SleepCoordinator(source::SleepMonitor* monitor,
                                   ConnectionCoordinator* connections, StreamingProbe isStreaming,
                                   QObject* parent)
    : SleepCoordinator(
          monitor,
          [connections](reducer::SleepEffect effect) { applyToConnections(connections, effect); },
          std::move(isStreaming), parent) {}

SleepCoordinator::SleepCoordinator(source::SleepMonitor* monitor, EffectSink sink,
                                   StreamingProbe isStreaming, QObject* parent)
    : QObject(parent), sink_(std::move(sink)), isStreaming_(std::move(isStreaming)) {
    if (monitor == nullptr) { return; }
    QObject::connect(monitor, &source::SleepMonitor::preparingForSleep, this,
                     &SleepCoordinator::onPreparingForSleep);
    QObject::connect(monitor, &source::SleepMonitor::lidPositionChanged, this,
                     &SleepCoordinator::onLidPositionChanged);
}

void SleepCoordinator::onPreparingForSleep(bool starting) {
    const auto reduction = reducer::reduceSleepCycle(phase_, starting);
    phase_ = reduction.next;
    if (reduction.effect == reducer::SleepEffect::None) { return; }
    const bool streaming = isStreaming_ && isStreaming_();
    const auto lidReduction = reducer::reduceLidOnSleepEffect(lid_, reduction.effect, streaming);
    lid_ = lidReduction.next;
    if (sink_) { sink_(reduction.effect); }
    announce(lidReduction.notice);
}

void SleepCoordinator::onLidPositionChanged(reducer::LidPosition position) {
    lid_ = reducer::reduceLidChange(lid_, position).next;
    const auto name = reducer::lidPositionName(position);
    qCInfo(lcDishSleep).noquote() << "lid"
                                  << QString::fromLatin1(name.data(),
                                                         static_cast<qsizetype>(name.size()));
}

void SleepCoordinator::announce(reducer::LidNotice notice) {
    switch (notice) {
    case reducer::LidNotice::None:
        return;
    case reducer::LidNotice::PausedWhileLidClosed:
        qCInfo(lcDishSleep) << "resumed: streaming was paused while the lid was closed";
        emit pausedWhileLidClosed();
        return;
    }
}

} // namespace dish::composer
