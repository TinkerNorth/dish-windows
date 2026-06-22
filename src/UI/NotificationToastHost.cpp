// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "NotificationToastHost.h"

#include "NotificationQueue.h"
#include "Theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// Width of the leading severity rail (the coloured strip on the left of the
// pill). Mirrors dish-android's RAIL_WIDTH_DP — same 4 dp on the design spec.
constexpr int kRailWidthPx = 4;

// Max width of the toast strip. Leaves the parent's left/right gutters free
// so the dashboard stays visible above the toasts.
constexpr int kStripMaxWidth = 480;

// Fade durations. Short enough that a flurry of toasts (the silent-reconnect
// path can land a couple in a row) doesn't feel ponderous.
constexpr int kFadeInMs = 140;
constexpr int kFadeOutMs = 220;

QRgb railColor(models::DishNotification::Severity severity) {
    switch (severity) {
    case models::DishNotification::Severity::Info:
        return Theme::primary;
    case models::DishNotification::Severity::Success:
        return Theme::success;
    case models::DishNotification::Severity::Warn:
        return Theme::warning;
    case models::DishNotification::Severity::Error:
        return Theme::error;
    }
    return Theme::primary;
}

} // namespace

NotificationToastHost::NotificationToastHost(QWidget* parent) : QWidget(parent) {
    // Transparent overlay: the host is just a layout container; each toast
    // paints its own surface. Click-through is intentional — the toast widgets
    // themselves get the close button and absorb clicks within their bounds.
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);

    stack_ = new QVBoxLayout(this);
    stack_->setContentsMargins(0, 0, 0, 0);
    stack_->setSpacing(8);
    // Bottom-up stacking: the newest toast appears at the bottom of the
    // visible strip, older toasts shift up. addStretch above the stack keeps
    // the column anchored at the bottom of the host.

    if (parent != nullptr) {
        parent->installEventFilter(this);
        reposition();
        raise();
    }
}

void NotificationToastHost::attach(NotificationQueue* queue) {
    if (queue_ != nullptr) { QObject::disconnect(queue_, nullptr, this, nullptr); }
    queue_ = queue;
    if (queue_ == nullptr) { return; }
    QObject::connect(queue_, &NotificationQueue::notificationAdded, this,
                     &NotificationToastHost::onNotificationAdded);
    QObject::connect(queue_, &NotificationQueue::notificationDismissed, this,
                     &NotificationToastHost::onNotificationDismissed);
}

bool NotificationToastHost::eventFilter(QObject* watched, QEvent* event) {
    if (watched == parent() && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
        reposition();
        raise();
    }
    return QWidget::eventFilter(watched, event);
}

void NotificationToastHost::reposition() {
    auto* p = qobject_cast<QWidget*>(parent());
    if (p == nullptr) { return; }
    const int width = qMin(kStripMaxWidth, p->width() - 32);
    // Re-fit to the visible stack: when the queue is empty sizeHint() collapses
    // to the layout's empty content, so the host doesn't sit on the dashboard
    // intercepting clicks where the toasts aren't drawn.
    const int height = qMax(sizeHint().height(), 0);
    const int x = (p->width() - width) / 2;
    const int y = p->height() - height - 24;
    setGeometry(x, qMax(y, 0), width, height);
    setVisible(height > 0);
}

void NotificationToastHost::onNotificationAdded(const models::DishNotification& notification) {
    auto* card = new QWidget(this);
    card->setObjectName(QStringLiteral("toastCard"));
    const QString rail = QStringLiteral("#%1").arg(
        static_cast<quint32>(railColor(notification.severity)) & 0x00FFFFFF, 6, 16, QChar('0'));
    // Background: dark surface with a coloured leading rail. We paint the
    // rail via a left border rather than a separate child widget so a single
    // QSS rule carries the whole visual.
    card->setStyleSheet(QStringLiteral("QWidget#toastCard {"
                                       " background-color: %1;"
                                       " border: 1px solid %2;"
                                       " border-left: %3px solid %4;"
                                       " border-radius: 10px;"
                                       "}")
                            .arg(hex(Theme::surface), hex(Theme::outline))
                            .arg(kRailWidthPx)
                            .arg(rail));

    auto* row = new QHBoxLayout(card);
    row->setContentsMargins(14, 10, 10, 10);
    row->setSpacing(10);

    auto* text = new QLabel(notification.message, card);
    text->setWordWrap(true);
    text->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(hex(Theme::onSurface)));
    row->addWidget(text, 1);

    QPushButton* closeButton = nullptr;
    if (notification.dismissible) {
        // "×" close affordance. Plain text so we don't have to ship a fresh
        // icon asset for this — the QSS strips the default button chrome.
        closeButton = new QPushButton(QStringLiteral("×"), card);
        closeButton->setFlat(true);
        closeButton->setCursor(Qt::PointingHandCursor);
        closeButton->setFixedSize(20, 20);
        closeButton->setStyleSheet(QStringLiteral("QPushButton { color: %1; border: none; "
                                                  "background: transparent; font-size: 14px; }"
                                                  "QPushButton:hover { color: %2; }")
                                       .arg(hex(Theme::muted), hex(Theme::onSurface)));
        const int id = notification.id;
        QObject::connect(closeButton, &QPushButton::clicked, this, [this, id] { dismissById(id); });
        row->addWidget(closeButton, 0, Qt::AlignTop);
    }

    auto* effect = new QGraphicsOpacityEffect(card);
    effect->setOpacity(0.0);
    card->setGraphicsEffect(effect);

    stack_->addWidget(card);

    Entry entry;
    entry.widget = card;
    entry.effect = effect;
    if (notification.durationMs > 0) {
        // PERSISTENT (= 0) banners stay until dismissById; everything else
        // auto-dismisses after `durationMs`. The timer is parented to the
        // host so a teardown cancels it cleanly.
        entry.autoDismiss = new QTimer(this);
        entry.autoDismiss->setSingleShot(true);
        entry.autoDismiss->setInterval(notification.durationMs);
        const int id = notification.id;
        QObject::connect(entry.autoDismiss, &QTimer::timeout, this,
                         [this, id] { dismissById(id); });
        entry.autoDismiss->start();
    }
    entries_.insert(notification.id, entry);

    // Fade-in. We don't parent the animation to the card so a card->deleteLater
    // mid-fade doesn't double-free the animation; setting QObject parent here
    // ties it to the host instead.
    auto* anim = new QPropertyAnimation(effect, "opacity", this);
    anim->setDuration(kFadeInMs);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->start(QAbstractAnimation::DeleteWhenStopped);

    reposition();
}

void NotificationToastHost::onNotificationDismissed(int id) { dismissById(id); }

void NotificationToastHost::dismissById(int id) {
    auto it = entries_.find(id);
    if (it == entries_.end()) { return; }
    Entry entry = it.value();
    entries_.erase(it);

    if (entry.autoDismiss != nullptr) {
        entry.autoDismiss->stop();
        entry.autoDismiss->deleteLater();
    }
    if (entry.widget == nullptr) { return; }

    auto* widget = entry.widget;
    auto* effect = entry.effect;
    // Fade-out then delete. The widget stays in the layout for the fade
    // duration so the strip doesn't visibly jump while the toast fades.
    if (effect != nullptr) {
        auto* anim = new QPropertyAnimation(effect, "opacity", this);
        anim->setDuration(kFadeOutMs);
        anim->setStartValue(effect->opacity());
        anim->setEndValue(0.0);
        QObject::connect(anim, &QPropertyAnimation::finished, widget, [this, widget] {
            stack_->removeWidget(widget);
            widget->deleteLater();
            reposition();
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    } else {
        stack_->removeWidget(widget);
        widget->deleteLater();
        reposition();
    }
}

} // namespace dish::ui
