// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "DeadzoneSettingsView.h"

#include "Theme.h"
#include "repository/DeadzoneRepository.h"
#include "source/store/MotionEnabledStore.h"

#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// The default deadzone profile (also installed by SDLGamepadBridge at attach):
// ~10 % of the int16 stick range and ~5 % of the 0..255 trigger range.
constexpr int kDefaultStickFlat = 3277;
constexpr int kDefaultTriggerFlat = 13;

// Slider upper bounds. Sticks tune within the first ~30 % of the int16 range
// (beyond that a deadzone is unusable); triggers cover the whole 0..255 range.
constexpr int kStickSliderMax = 10000;
constexpr int kTriggerSliderMax = 255;

} // namespace

DeadzoneSettingsView::DeadzoneSettingsView(repository::DeadzoneRepository* repo,
                                           source::MotionEnabledStore* motionStore,
                                           const QList<DeviceRow>& devices, QWidget* parent)
    : QWidget(parent), repo_(repo), motionStore_(motionStore) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* headerRow = new QHBoxLayout;
    auto* title = new QLabel(tr("Dead zones"), this);
    title->setStyleSheet(
        QStringLiteral("font-size: 17px; font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* doneButton = new QPushButton(tr("Done"), this);
    QObject::connect(doneButton, &QPushButton::clicked, this,
                     &DeadzoneSettingsView::closeRequested);
    headerRow->addWidget(title, 1, Qt::AlignVCenter);
    headerRow->addWidget(doneButton, 0, Qt::AlignVCenter);
    layout->addLayout(headerRow);

    auto* sectionHeader = new QLabel(tr("CONTROLLERS"), this);
    sectionHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(sectionHeader);

    if (devices.isEmpty()) {
        auto* empty = new QLabel(tr("Connect a controller to adjust its dead zones."), this);
        empty->setWordWrap(true);
        empty->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
        layout->addWidget(empty);
    } else {
        auto* column = new QVBoxLayout;
        column->setSpacing(10);
        for (const auto& device : devices) { addDeviceCard(column, device); }
        layout->addLayout(column);
    }

    auto* footnote =
        new QLabel(tr("A larger dead zone ignores more stick or trigger travel near rest — "
                      "raise it if a worn controller drifts on its own."),
                   this);
    footnote->setWordWrap(true);
    footnote->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    layout->addWidget(footnote);

    layout->addStretch(1);
}

void DeadzoneSettingsView::addDeviceCard(QVBoxLayout* into, const DeviceRow& device) {
    // Seed from the repo; fall back to the default profile when unset.
    input::deadzone::Deadzones dz{static_cast<std::int16_t>(kDefaultStickFlat),
                                  static_cast<std::uint8_t>(kDefaultTriggerFlat)};
    if (repo_ != nullptr) {
        if (auto stored = repo_->deadzonesFor(device.id)) { dz = *stored; }
    }

    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(8);

    auto* name = new QLabel(device.name.isEmpty() ? device.id : device.name, card);
    name->setStyleSheet(QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    cardLayout->addWidget(name);

    // The two sliders share a tiny live-update lambda that re-reads both, writes
    // the repo and emits. `deviceId` is captured by value so it survives the
    // device list going out of scope.
    const QString deviceId = device.id;

    auto* stickValue = new QLabel(card);
    auto* triggerValue = new QLabel(card);
    const auto valueQss = QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted));
    stickValue->setStyleSheet(valueQss);
    triggerValue->setStyleSheet(valueQss);

    auto* stickSlider = new QSlider(Qt::Horizontal, card);
    stickSlider->setRange(0, kStickSliderMax);
    stickSlider->setValue(dz.stickFlat);
    auto* triggerSlider = new QSlider(Qt::Horizontal, card);
    triggerSlider->setRange(0, kTriggerSliderMax);
    triggerSlider->setValue(dz.triggerFlat);

    const auto refreshLabels = [stickValue, triggerValue, stickSlider, triggerSlider] {
        stickValue->setText(tr("Stick: %1").arg(stickSlider->value()));
        triggerValue->setText(tr("Trigger: %1").arg(triggerSlider->value()));
    };
    refreshLabels();

    const auto pushChange = [this, deviceId, stickSlider, triggerSlider, refreshLabels] {
        refreshLabels();
        input::deadzone::Deadzones next{static_cast<std::int16_t>(stickSlider->value()),
                                        static_cast<std::uint8_t>(triggerSlider->value())};
        onDeadzoneChanged(deviceId, next);
    };
    QObject::connect(stickSlider, &QSlider::valueChanged, this, pushChange);
    QObject::connect(triggerSlider, &QSlider::valueChanged, this, pushChange);

    auto* stickRow = new QHBoxLayout;
    stickRow->addWidget(stickSlider, 1);
    stickRow->addWidget(stickValue, 0, Qt::AlignVCenter);
    auto* triggerRow = new QHBoxLayout;
    triggerRow->addWidget(triggerSlider, 1);
    triggerRow->addWidget(triggerValue, 0, Qt::AlignVCenter);
    cardLayout->addLayout(stickRow);
    cardLayout->addLayout(triggerRow);

    // Motion-forwarding toggle — only meaningful for a pad that has a gyro.
    if (device.hasGyro && motionStore_ != nullptr) {
        const std::string slotKey = deviceId.toStdString();
        auto* motionToggle = new QCheckBox(tr("Forward motion"), card);
        motionToggle->setChecked(motionStore_->isEnabled(slotKey));
        motionToggle->setStyleSheet(QStringLiteral("color: %1;").arg(hex(Theme::onSurface)));
        QObject::connect(motionToggle, &QCheckBox::toggled, this, [this, slotKey](bool on) {
            if (motionStore_ != nullptr) { motionStore_->setEnabled(slotKey, on); }
        });
        cardLayout->addWidget(motionToggle);
    }

    into->addWidget(card);
}

void DeadzoneSettingsView::onDeadzoneChanged(const QString& deviceId,
                                             input::deadzone::Deadzones dz) {
    if (repo_ != nullptr) { repo_->setDeadzones(deviceId, dz); }
    emit deadzoneChanged(deviceId, dz);
}

} // namespace dish::ui
