// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "DisplaySleepInhibitor.h"

#include <QLoggingCategory>

// NOMINMAX strips the min/max macros that otherwise fight std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace dish::util {

namespace {

Q_LOGGING_CATEGORY(lcDishWake, "dish.wake")

} // namespace

SetThreadExecutionStateInhibitor::SetThreadExecutionStateInhibitor(QObject* parent)
    : DisplaySleepInhibitor(parent) {}

SetThreadExecutionStateInhibitor::~SetThreadExecutionStateInhibitor() {
    // ES_CONTINUOUS alone clears our "...REQUIRED" flags without disturbing
    // other applications' assertions.
    if (held_) { ::SetThreadExecutionState(ES_CONTINUOUS); }
}

void SetThreadExecutionStateInhibitor::acquire(const QString& reason) {
    if (held_) { return; }
    // ES_AWAYMODE_REQUIRED is deliberately omitted: it can surface the process
    // as "Media playing" in Task Scheduler.
    const EXECUTION_STATE prev =
        ::SetThreadExecutionState(ES_CONTINUOUS | ES_DISPLAY_REQUIRED | ES_SYSTEM_REQUIRED);
    if (prev == 0) {
        qCWarning(lcDishWake) << "SetThreadExecutionState(acquire) failed";
        return;
    }
    held_ = true;
    qCDebug(lcDishWake) << "SetThreadExecutionState acquired; reason=" << reason;
}

void SetThreadExecutionStateInhibitor::release() {
    if (!held_) { return; }
    const EXECUTION_STATE prev = ::SetThreadExecutionState(ES_CONTINUOUS);
    if (prev == 0) {
        qCWarning(lcDishWake) << "SetThreadExecutionState(release) failed";
        return;
    }
    held_ = false;
    qCDebug(lcDishWake) << "SetThreadExecutionState released";
}

} // namespace dish::util
