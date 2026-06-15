// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"
#include "source/notification/DishNotifications.h"

#include <QMetaType>
#include <QObject>
#include <QString>

// Register the typed notification with Qt's meta-type system so the
// `notificationAdded` signal can survive a queued connection (e.g. a future
// background poster). Today every emit/slot pair runs on the UI thread, but
// declaring it here costs nothing and avoids the silent connection failure
// pattern if that ever changes.
Q_DECLARE_METATYPE(dish::models::DishNotification)

namespace dish::ui {

// UI-layer adapter over the source-layer [source::DishNotifications] event
// channel. The id assignment + severity->duration defaults + the bounded
// DROP_OLDEST post/dismiss channels now live in the source class (the business
// owner, mirroring dish-android source/notification/DishNotifications); this
// queue forwards into it and re-emits its signals under the names the renderer
// (NotificationToastHost) already binds to, so the UI wiring is unchanged.
//
// Same-kind de-duplication and dismiss-on-action still live in the renderer
// (NotificationToastHost), the way the Android DishNotifications.Attachment owns
// dedup — this queue stays view-free.
class NotificationQueue : public QObject {
    Q_OBJECT
  public:
    explicit NotificationQueue(QObject* parent = nullptr);

    // The underlying source-layer notifications owner (for non-UI consumers that
    // want the raw event channels). Owned by this queue.
    source::DishNotifications* source() { return source_; }

    // Post a notification. Returns the assigned id so a caller can later
    // dismiss it explicitly (the equivalent of a `notification.dismiss()` in
    // Android once the underlying state clears). The id is monotonic and
    // process-local — it's a handle, not a stable identity.
    int post(models::DishNotification notification);

    // Convenience: post a short error toast for `message`. Used by the
    // AppModel adapter that bridges the legacy `errorMessage(QString)` signal
    // into the typed queue, and by the network layer's user-facing errors.
    int postError(const QString& message);

    // Drop a previously-posted notification by id. No-op if the renderer
    // already aged it out.
    void dismiss(int id);

  signals:
    // Fired immediately on post(). The renderer connects to this to push the
    // notification onto its visible stack.
    void notificationAdded(const dish::models::DishNotification& notification);

    // Fired on dismiss(). The renderer animates the matching toast out.
    void notificationDismissed(int id);

  private:
    source::DishNotifications* source_;
};

} // namespace dish::ui
