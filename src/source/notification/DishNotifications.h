// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DishNotifications — the source-layer owner of the in-app notification event
// channel. Mirrors dish-android source/notification/DishNotifications: a class
// that owns TWO bounded DROP_OLDEST event channels (posts + dismissals), assigns
// monotonic ids, and applies severity->duration defaults. This is the business/
// source role beneath the UI-layer NotificationQueue/NotificationToastHost; the
// renderer keeps owning same-kind dedup + dismiss-on-action (android's
// Attachment), so this class is purely the post/dismiss bus.
//
// It is one of the only two "rare event-emitting" classes in the design (the
// other is the connection manager) — Sources otherwise expose state, not events
// (see architecture/README + android-architecture rule 15). It exposes the
// post/dismiss channels directly (for non-UI consumers + tests) AND Qt signals
// (for the widget renderer), the same value reaching both.
//
// SoC: id assignment + duration defaults live HERE (the source), not in the
// renderer. The DROP_OLDEST overflow protects a poster that outruns a detached
// renderer without unbounded growth.

#pragma once

#include "Models/Models.h"
#include "source/notification/EventChannel.h"

#include <QObject>
#include <QString>

namespace dish::source {

class DishNotifications : public QObject {
    Q_OBJECT
  public:
    // Channel buffer capacity before DROP_OLDEST kicks in. Matches the spirit of
    // android's extraBufferCapacity for the notification flows.
    static constexpr int kChannelCapacity = 16;

    explicit DishNotifications(QObject* parent = nullptr);

    // The default display duration (ms) for a severity, used when a posted
    // notification leaves durationMs at the kUseSeverityDefault sentinel. Pure +
    // static so it's testable without an instance. Maps onto the existing
    // DishNotification duration sentinels:
    //   Info / Success -> short (3.5 s), Warn / Error -> long (6 s).
    static int defaultDurationForSeverity(models::DishNotification::Severity severity);

    // Sentinel a caller leaves in durationMs to request the severity default.
    // (Distinct from kDurationPersistent == 0, which means "stay until dismissed".)
    static constexpr int kUseSeverityDefault = -1;

    // Post a notification onto the post channel. Assigns a fresh monotonic id
    // (process-local; a dismiss handle, not a stable identity) and resolves the
    // severity default duration if requested. Returns the assigned id.
    int post(models::DishNotification notification);

    // Convenience: post a short error toast for `message` (the legacy
    // errorMessage(QString) adapter). Returns the assigned id.
    int postError(const QString& message);

    // Dismiss a previously-posted notification by id onto the dismiss channel.
    void dismiss(int id);

    // The raw event channels (for non-widget consumers + tests). The renderer
    // uses the Qt signals below; these are the SharedFlow analogues.
    EventChannel<models::DishNotification>& posts() { return posts_; }
    EventChannel<int>& dismissals() { return dismissals_; }

  signals:
    // Emitted on every post(), carrying the id-stamped, duration-resolved
    // notification. The renderer connects here to push onto its visible stack.
    void notificationPosted(const dish::models::DishNotification& notification);
    // Emitted on every dismiss(id). The renderer animates the matching toast out.
    void notificationDismissed(int id);

  private:
    int nextId_ = 1;
    EventChannel<models::DishNotification> posts_{kChannelCapacity};
    EventChannel<int> dismissals_{kChannelCapacity};
};

} // namespace dish::source
