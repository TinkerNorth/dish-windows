// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

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

// Process-wide bus for [models::DishNotification]. Mirrors dish-android's
// `DishNotifications` (source/notification/DishNotifications.kt) — emitters
// post typed notifications, the toast host renders them as a stacked strip.
//
// The queue is intentionally a pure signal hub: it owns id assignment and the
// `errorMessage → enqueue` adapter only. Same-kind de-duplication and
// dismiss-on-action live in the renderer (NotificationToastHost), the way
// the Android DishNotifications.Attachment owns dedup. Keeping the queue
// view-free leaves it cheap to drive from a future headless / service mode.
class NotificationQueue : public QObject {
    Q_OBJECT
  public:
    explicit NotificationQueue(QObject* parent = nullptr);

    // Post a notification. Returns the assigned id so a caller can later
    // dismiss it explicitly (the equivalent of a `notification.dismiss()` in
    // Android once the underlying state clears). The id is monotonic and
    // process-local — it's a handle, not a stable identity.
    int post(models::DishNotification notification);

    // Convenience: post a short error toast for `message`. Used by the
    // AppModel adapter that bridges the legacy `errorMessage(QString)` signal
    // into the typed queue, and by the network layer's user-facing errors.
    int postError(const QString& message);

    // Convenience: post a short warning toast for `message` — same shape as
    // postError but with Warn severity (amber rail rather than red) and a
    // distinct kind tag so the renderer can dedupe runs of the same warning.
    // Used by the AppModel adapter that bridges the `warningMessage(QString)`
    // signal — today the only emitter is the motion-delivery branch of the
    // controller-ACK path.
    int postWarning(const QString& message);

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
    int nextId_ = 1;
};

} // namespace dish::ui
