// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "NotificationQueue.h"

namespace dish::ui {

NotificationQueue::NotificationQueue(QObject* parent) : QObject(parent) {}

int NotificationQueue::post(models::DishNotification notification) {
    // Assign the id centrally so callers can hand us a literal struct without
    // having to thread a counter through every emit site. Matches Android's
    // `DishNotifications.post`: the constructor is internal, the id is
    // monotonic, the caller gets it back as a dismiss handle.
    const int id = nextId_++;
    notification.id = id;
    emit notificationAdded(notification);
    return id;
}

int NotificationQueue::postError(const QString& message) {
    models::DishNotification n;
    n.severity = models::DishNotification::Severity::Error;
    n.kind = QStringLiteral("error");
    n.message = message;
    n.durationMs = models::DishNotification::kDurationLongMs;
    return post(std::move(n));
}

int NotificationQueue::postWarning(const QString& message) {
    models::DishNotification n;
    n.severity = models::DishNotification::Severity::Warn;
    n.kind = QStringLiteral("warning");
    n.message = message;
    n.durationMs = models::DishNotification::kDurationLongMs;
    return post(std::move(n));
}

void NotificationQueue::dismiss(int id) { emit notificationDismissed(id); }

} // namespace dish::ui
