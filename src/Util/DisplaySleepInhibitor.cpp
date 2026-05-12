// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "DisplaySleepInhibitor.h"

#include <QLoggingCategory>

// Keep windows.h out of any other TU; this is the only file that needs it.
// NOMINMAX strips the min/max macros that fight std::min/std::max everywhere.
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
    // RAII: hand the flag back if the AppModel forgot to release. Calling
    // SetThreadExecutionState(ES_CONTINUOUS) clears every "...REQUIRED" flag
    // we previously set without disturbing other applications' assertions.
    if (held_) { ::SetThreadExecutionState(ES_CONTINUOUS); }
}

void SetThreadExecutionStateInhibitor::acquire(const QString& reason) {
    if (held_) { return; }
    // ES_CONTINUOUS  — keep this set of flags in force until cleared.
    // ES_DISPLAY_REQUIRED — defer monitor blanking.
    // ES_SYSTEM_REQUIRED  — defer system idle sleep (S3 / Modern Standby).
    // Older Windows had ES_AWAYMODE_REQUIRED for media-server scenarios; we
    // skip it because it can show "Media playing" in Task Scheduler.
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
