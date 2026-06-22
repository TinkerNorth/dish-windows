// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ui/common/ExternalLink.h"

#include "Models/Models.h"
#include "UI/NotificationQueue.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QUrl>

namespace dish::ui {

bool openExternalUrl(const QString& url, NotificationQueue* notifications) {
    const bool ok = QDesktopServices::openUrl(QUrl(url));
    if (!ok && notifications != nullptr) {
        models::DishNotification n;
        n.kind = QStringLiteral("external-url-failed");
        n.severity = models::DishNotification::Severity::Warn;
        // Mirrors android's notifications.warn(title = "Couldn't open browser").
        n.message = QCoreApplication::translate("dish::ui::ExternalLink", "Couldn't open browser");
        notifications->post(std::move(n));
    }
    return ok;
}

} // namespace dish::ui
