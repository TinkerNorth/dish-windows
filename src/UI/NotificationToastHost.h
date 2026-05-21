// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QPropertyAnimation;
class QTimer;
class QVBoxLayout;

namespace dish::ui {

class NotificationQueue;

// Renders a stacked strip of toasts at the bottom-center of `parent`. Mirrors
// dish-android's `MaterialSnackbarRenderer` collapsed with its `Attachment`:
// one host per main window, listening to a single [NotificationQueue]. Same
// rough silhouette as the Android renderer (rail-coloured pill, bold title,
// fade-in / fade-out).
//
// Pragmatic on animation: each toast fades in over ~140 ms, sits for its
// `durationMs`, then fades out. No per-toast slide / drop transforms — Qt's
// QGraphicsOpacityEffect carries the entire visual indicator.
class NotificationToastHost : public QWidget {
    Q_OBJECT
  public:
    explicit NotificationToastHost(QWidget* parent = nullptr);

    // Subscribe to a queue. The host can attach to a single queue at a time;
    // calling attach again replaces the prior connection. Decoupled from the
    // constructor so MainWindow can build the host before the AppModel is
    // wired.
    void attach(NotificationQueue* queue);

  protected:
    // We anchor ourselves to the bottom-center of the parent on every
    // resize/show — the host has no fixed geometry of its own, it's a thin
    // overlay above the dashboard.
    bool eventFilter(QObject* watched, QEvent* event) override;

  private:
    void onNotificationAdded(const models::DishNotification& notification);
    void onNotificationDismissed(int id);
    void dismissById(int id);
    void reposition();

    NotificationQueue* queue_ = nullptr;
    QVBoxLayout* stack_;
    // Per-toast bookkeeping. The auto-dismiss timer is owned by the host so
    // an explicit dismissById from the close button can stop it before it
    // fires (which would otherwise double-dismiss a banner the user just
    // tapped to close).
    struct Entry {
        QWidget* widget = nullptr;
        QGraphicsOpacityEffect* effect = nullptr;
        QTimer* autoDismiss = nullptr;
    };
    QHash<int, Entry> entries_;
};

} // namespace dish::ui
