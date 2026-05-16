// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "SlotCard.h"

#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

namespace dish::ui {

SlotCard::SlotCard(QWidget* parent) : QFrame(parent) {
    setObjectName(QStringLiteral("card"));
    setFrameShape(QFrame::NoFrame);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);

    dot_ = new QLabel(this);
    dot_->setFixedSize(8, 8);
    dot_->setStyleSheet(dotQss(Theme::muted));

    auto* textLayout = new QVBoxLayout;
    textLayout->setSpacing(2);
    nameLabel_ = new QLabel(this);
    nameLabel_->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    boundLabel_ = new QLabel(this);
    boundLabel_->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    textLayout->addWidget(nameLabel_);
    textLayout->addWidget(boundLabel_);

    // Capability chip row. Mirrors dish-mac's capabilityRow under the name +
    // status lines. Today it carries the one motion chip; touchpad / rumble /
    // battery chips can join the same row when those reach the Windows UI.
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(6);
    chipRow->setContentsMargins(0, 4, 0, 0);
    motionChip_ = new QLabel(this);
    chipRow->addWidget(motionChip_, 0, Qt::AlignVCenter);
    chipRow->addStretch(1);
    textLayout->addLayout(chipRow);

    bindButton_ = new QPushButton(this);
    QObject::connect(bindButton_, &QPushButton::clicked, this, &SlotCard::onBindClicked);

    layout->addWidget(dot_, 0, Qt::AlignVCenter);
    layout->addLayout(textLayout, 1);
    layout->addWidget(bindButton_, 0, Qt::AlignVCenter);
}

void SlotCard::setSlot(const models::ControllerSlot& slot,
                       const QList<models::ConnectionSummary>& available) {
    slot_ = slot;
    available_ = available;
    nameLabel_->setText(slot.name);

    // Motion-capability chip. Two explicit, mutually-exclusive states so a
    // player is never left guessing whether motion is off or simply absent:
    //   * hardware HAS an IMU  -> "Gyro" chip, primary-tinted. dish-windows
    //     has no motion on/off setting, so a capable pad is always forwarding
    //     gyro/accelerometer — the tooltip says so outright.
    //   * hardware has NO IMU  -> "No gyro" chip, dimmed + outlined. This is
    //     drawn rather than omitted so "not available" is visible, not merely
    //     the absence of an indicator (e.g. an Xbox pad).
    // If a motion on/off setting is ever added, this chip should gain a third
    // "available, off" state (dimmed but with the "Gyro" label) — see the
    // dish-mac CapabilityChip, which keys its colour off FeatureSettings.
    const bool hasMotion = slot.capabilities.hasMotion;
    motionChip_->setText(hasMotion ? QStringLiteral("Gyro") : QStringLiteral("No gyro"));
    motionChip_->setStyleSheet(capabilityChipQss(hasMotion));
    motionChip_->setToolTip(
        hasMotion
            ? QStringLiteral("Motion available — this controller has a gyro/accelerometer "
                             "and motion aiming is being forwarded.")
            : QStringLiteral("Motion not available — this controller has no gyro/accelerometer, "
                             "so motion aiming can't be forwarded."));

    if (slot.boundStatus.has_value()) {
        boundLabel_->setText(QStringLiteral("Bound to %1").arg(slot.boundStatus->label));
        const auto color = slot.boundStatus->live == models::ConnectionLive::Connected
                               ? Theme::success
                               : Theme::warning;
        dot_->setStyleSheet(dotQss(color));
        bindButton_->setText(QStringLiteral("Unbind"));
    } else {
        boundLabel_->setText(QStringLiteral("Unbound"));
        dot_->setStyleSheet(dotQss(Theme::muted));
        bindButton_->setText(QStringLiteral("Bind\u2026"));
    }
    bindButton_->setEnabled(slot.boundConnectionId.has_value() || !available.isEmpty());
}

void SlotCard::onBindClicked() {
    if (slot_.boundConnectionId.has_value()) {
        emit unbindRequested(slot_.id);
        return;
    }
    if (available_.isEmpty()) { return; }
    QMenu menu(this);
    for (const auto& c : available_) {
        auto* act = menu.addAction(c.label);
        const QString cid = c.id;
        QObject::connect(act, &QAction::triggered, this,
                         [this, cid] { emit bindRequested(slot_.id, cid); });
    }
    menu.exec(bindButton_->mapToGlobal(QPoint(0, bindButton_->height())));
}

} // namespace dish::ui
