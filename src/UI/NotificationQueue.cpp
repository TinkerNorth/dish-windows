// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "NotificationQueue.h"

namespace dish::ui {

NotificationQueue::NotificationQueue(QObject* parent)
    : QObject(parent), source_(new source::DishNotifications(this)) {
    // Re-emit the source channel's events under the names the renderer binds to,
    // so NotificationToastHost (READS-ONLY) stays wired unchanged.
    QObject::connect(source_, &source::DishNotifications::notificationPosted, this,
                     &NotificationQueue::notificationAdded);
    QObject::connect(source_, &source::DishNotifications::notificationDismissed, this,
                     &NotificationQueue::notificationDismissed);
}

int NotificationQueue::post(models::DishNotification notification) {
    // Delegate to the source: it owns id assignment + duration defaults.
    return source_->post(std::move(notification));
}

int NotificationQueue::postError(const QString& message) { return source_->postError(message); }

void NotificationQueue::dismiss(int id) { source_->dismiss(id); }

} // namespace dish::ui
