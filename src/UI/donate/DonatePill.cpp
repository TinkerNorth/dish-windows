// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/donate/DonatePill.h"

#include "UI/Theme.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>

namespace dish::ui {

// The pure dismiss-window decision + persistence helpers live in
// DonatePillLogic.cpp (dish_core) so they're host-testable; the widget consumes
// them here.

DonatePill::DonatePill(std::shared_ptr<QSettings> settings, QWidget* parent)
    : QWidget(parent),
      settings_(settings
                    ? std::move(settings)
                    : std::make_shared<QSettings>(QStringLiteral("Dish"), QStringLiteral("Dish"))) {
    auto* row = new QHBoxLayout(this);
    row->setContentsMargins(12, 8, 8, 8);
    row->setSpacing(8);

    // A surface pill with an accent border + heart — android's view_donate_pill
    // uses app:strokeColor="@color/colorPulse" (its donate accent) so the pill
    // reads as a donate invitation, not a neutral card. The pulse-pink accent has
    // no portable Theme token on Windows, so the border/heart use the cyan
    // `primary` accent (theme-correct under dark AND light — it reads the active
    // palette).
    setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: 16px;")
                      .arg(hex(Theme::surface), hex(Theme::primary)));

    auto* heart = new QLabel(QStringLiteral("♥"), this); // ♥ — brand glyph, not localized
    heart->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(hex(Theme::primary)));

    auto* label = new QLabel(tr("Support Dish"), this);
    label->setStyleSheet(
        QStringLiteral("color: %1; font-weight: 600; background: transparent; border: none;")
            .arg(hex(Theme::onSurface)));

    // The body (heart + label) opens the donate screen. A transparent overlay
    // button captures the click across both labels without stealing the dismiss
    // button's hit area.
    auto* openButton = new QPushButton(this);
    openButton->setFlat(true);
    openButton->setCursor(Qt::PointingHandCursor);
    openButton->setStyleSheet(QStringLiteral("border: none; background: transparent;"));
    QObject::connect(openButton, &QPushButton::clicked, this, &DonatePill::openRequested);

    auto* dismiss = new QPushButton(QStringLiteral("×"), this); // × — glyph, not localized
    dismiss->setToolTip(tr("Dismiss for a day"));
    dismiss->setFixedSize(20, 20);
    dismiss->setCursor(Qt::PointingHandCursor);
    dismiss->setStyleSheet(
        QStringLiteral("color: %1; border: none; background: transparent; font-size: 14px;")
            .arg(hex(Theme::muted)));
    QObject::connect(dismiss, &QPushButton::clicked, this, &DonatePill::onDismissClicked);

    // openButton sits under the labels (added first, stretched), dismiss on top.
    row->addWidget(openButton, 0);
    row->addWidget(heart, 0, Qt::AlignVCenter);
    row->addWidget(label, 1, Qt::AlignVCenter);
    row->addWidget(dismiss, 0, Qt::AlignVCenter);
    // Keep the open-button click area spanning the labels.
    openButton->raise();
    openButton->lower();

    refreshVisibility(QDateTime::currentMSecsSinceEpoch());
}

DonatePill::~DonatePill() = default;

void DonatePill::refreshVisibility(std::int64_t nowMs) {
    const std::int64_t dismissedAt = donatePillReadDismissedAt(*settings_);
    setVisible(!donatePillSuppressed(dismissedAt, nowMs));
}

void DonatePill::onDismissClicked() {
    donatePillWriteDismissedAt(*settings_, QDateTime::currentMSecsSinceEpoch());
    hide();
}

} // namespace dish::ui
