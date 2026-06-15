// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/CrashReportingBackend.h"

#include <QLoggingCategory>

namespace dish::composer {

namespace {
Q_LOGGING_CATEGORY(lcCrash, "dish.crash")
} // namespace

void NoopCrashReportingBackend::setEnabled(bool enabled) {
    // No backend this wave (D4). Record the flip so a developer can confirm the
    // opt-in/opt-out plumbing fires; a real backend (minidump / Sentry /
    // Crashpad) would replace this body.
    qCInfo(lcCrash) << "crash-reporting collection set to" << enabled
                    << "(no backend wired — D4 deferred)";
}

} // namespace dish::composer
