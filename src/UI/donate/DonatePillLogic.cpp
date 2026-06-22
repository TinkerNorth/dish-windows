// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The PURE dismiss-window decision + the trivial persistence helpers for the
// donate pill (Workstream 3b), split out of the widget (DonatePill.cpp) so they
// live in dish_core and are host-testable without a QWidget / a clock. The
// widget in DonatePill.cpp consumes these. See DonatePill.h.

#include "ui/donate/DonatePill.h"

#include <QSettings>

namespace dish::ui {

bool donatePillSuppressed(std::int64_t dismissedAtMs, std::int64_t nowMs, std::int64_t windowMs) {
    return (nowMs - dismissedAtMs) < windowMs;
}

std::int64_t donatePillReadDismissedAt(QSettings& settings) {
    return settings.value(QLatin1String(kDonatePillDismissedAtKey), 0).toLongLong();
}

void donatePillWriteDismissedAt(QSettings& settings, std::int64_t dismissedAtMs) {
    settings.setValue(QLatin1String(kDonatePillDismissedAtKey),
                      static_cast<qlonglong>(dismissedAtMs));
}

} // namespace dish::ui
