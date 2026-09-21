// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/SleepMonitor.h"

#include <QCoreApplication>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace dish::source {

WindowsPowerSleepMonitor::WindowsPowerSleepMonitor(QObject* parent) : SleepMonitor(parent) {}

WindowsPowerSleepMonitor::~WindowsPowerSleepMonitor() { stop(); }

void WindowsPowerSleepMonitor::start() {
    if (started_) { return; }
    auto* app = QCoreApplication::instance();
    if (app == nullptr) { return; }
    app->installNativeEventFilter(this);
    started_ = true;
}

void WindowsPowerSleepMonitor::stop() {
    if (!started_) { return; }
    if (auto* app = QCoreApplication::instance()) { app->removeNativeEventFilter(this); }
    started_ = false;
}

std::optional<bool> WindowsPowerSleepMonitor::edgeFor(unsigned long long powerEvent) {
    switch (powerEvent) {
    case PBT_APMSUSPEND:
        return true;
    case PBT_APMRESUMEAUTOMATIC:
    case PBT_APMRESUMESUSPEND:
        return false;
    default:
        return std::nullopt;
    }
}

bool WindowsPowerSleepMonitor::nativeEventFilter(const QByteArray& eventType, void* message,
                                                 qintptr* /*result*/) {
    if (message == nullptr || eventType != "windows_generic_MSG") { return false; }
    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_POWERBROADCAST) { return false; }
    if (const auto edge = edgeFor(msg->wParam)) { emit preparingForSleep(*edge); }
    return false;
}

} // namespace dish::source
