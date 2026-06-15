// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QDialog>

namespace dish {
class FeatureSettings;
}

namespace dish::ui {

// Modal host for the shared SettingsView. dish-windows presents settings as a
// modal sheet (like ConnectionsDialog / PairingDialog); the actual controls
// live in SettingsView so they stay shared with dish-linux.
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    explicit SettingsDialog(FeatureSettings* settings, QWidget* parent = nullptr);

  signals:
    // Forwarded from the hosted SettingsView when the user asks for the
    // per-device dead-zone / motion page (Workstream 2d). MainWindow opens the
    // DeadzoneSettingsView in response (it owns the device list + repositories).
    void deadzonesRequested();
};

} // namespace dish::ui
