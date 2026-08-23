// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/WakeInhibitor.h"

#include <QLoggingCategory>

// NOMINMAX strips the min/max macros that otherwise fight std::min/std::max.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcDishWake, "dish.wake")

// ES_AWAYMODE_REQUIRED is deliberately omitted: it can surface the process as
// "Media playing" in Task Scheduler. ES_CONTINUOUS alone clears our own
// "...REQUIRED" flags without disturbing another application's assertion.
EXECUTION_STATE flagsFor(reducer::KeepAwakeReach reach) {
    switch (reach) {
    case reducer::KeepAwakeReach::System:
        return ES_CONTINUOUS | ES_SYSTEM_REQUIRED;
    case reducer::KeepAwakeReach::SystemAndDisplay:
        return ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED;
    case reducer::KeepAwakeReach::None:
        break;
    }
    return ES_CONTINUOUS;
}

} // namespace

SetThreadExecutionStateInhibitor::SetThreadExecutionStateInhibitor(QObject* parent)
    : WakeInhibitor(parent) {}

SetThreadExecutionStateInhibitor::~SetThreadExecutionStateInhibitor() {
    if (held_ != reducer::KeepAwakeReach::None) { ::SetThreadExecutionState(ES_CONTINUOUS); }
}

void SetThreadExecutionStateInhibitor::apply(reducer::KeepAwakeReach reach, const QString& reason) {
    if (reach == held_) { return; }
    if (::SetThreadExecutionState(flagsFor(reach)) == 0) {
        qCWarning(lcDishWake) << "SetThreadExecutionState failed; reach unchanged";
        return;
    }
    held_ = reach;
    qCDebug(lcDishWake) << "SetThreadExecutionState reach=" << static_cast<int>(reach)
                        << "reason=" << reason;
}

} // namespace dish::source
