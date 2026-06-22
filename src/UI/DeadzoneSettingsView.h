// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/input/Deadzones.h"

#include <QList>
#include <QString>
#include <QWidget>

#include <cstdint>

class QVBoxLayout;

namespace dish::repository {
class DeadzoneRepository;
}
namespace dish::source {
class MotionEnabledStore;
}

namespace dish::ui {

// Per-device controller-tuning page (dead zones + motion forwarding). A distinct
// surface from the feature-toggle SettingsView: one card per attached controller
// carrying a stick-deadzone slider, a trigger-deadzone slider, and — for pads
// that report a gyro — a "Forward motion" toggle. New in Workstream 2d (Windows
// previously installed a single hard-coded deadzone default at device-attach
// with no per-device override, and had no motion-enable toggle).
//
// Self-contained QWidget so the desktop shell can host it however it likes
// (dish-windows wraps it in a modal QDialog). It persists each change to the
// injected DeadzoneRepository / MotionEnabledStore and emits deadzoneChanged so
// the host can push the deadzone value into the live GamepadInputProcessor once
// (the SDL bridge's existing setDeadzones seam) — never per input event, per the
// hot-path rule. The motion toggle is consumed at descriptor-build time, so it
// needs no live-push signal.
class DeadzoneSettingsView : public QWidget {
    Q_OBJECT
  public:
    // One attached controller the page renders a card for. `hasGyro` decides
    // whether the motion-forwarding toggle is offered.
    struct DeviceRow {
        QString id;
        QString name;
        bool hasGyro = false;
    };

    // `repo` / `motionStore` are owned by the AppModel and outlive this view.
    // `devices` is the current attached-controller list; the page seeds each
    // control from the stores (falling back to the default profile / motion-on
    // when a device has no override). `motionStore` may be null (deadzones-only).
    DeadzoneSettingsView(repository::DeadzoneRepository* repo,
                         source::MotionEnabledStore* motionStore, const QList<DeviceRow>& devices,
                         QWidget* parent = nullptr);

  signals:
    // Emitted after the user moves a slider (and the new value is persisted), so
    // the host applies it to the live processor for `deviceId`.
    void deadzoneChanged(const QString& deviceId, input::deadzone::Deadzones dz);
    // Emitted when the user dismisses the page (the Done button).
    void closeRequested();

  private:
    void addDeviceCard(QVBoxLayout* into, const DeviceRow& device);
    void onDeadzoneChanged(const QString& deviceId, input::deadzone::Deadzones dz);

    repository::DeadzoneRepository* repo_;
    source::MotionEnabledStore* motionStore_;
};

} // namespace dish::ui
