// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QDialog>

namespace dish {
class FeatureSettings;
}

namespace dish::source {
class ThemePreferenceStore;
class CrashReportingStore;
} // namespace dish::source

namespace dish::ui {

class NotificationQueue;

// Modal host for the shared SettingsView. dish-windows presents settings as a
// modal sheet (like ConnectionsDialog / PairingDialog); the actual controls
// live in SettingsView so they stay shared with dish-linux.
//
// Forwards the SettingsView's screen-open requests up to the host (MainWindow),
// which owns the dialogs/pages those screens live in.
class SettingsDialog : public QDialog {
    Q_OBJECT
  public:
    SettingsDialog(FeatureSettings* settings, source::ThemePreferenceStore* themeStore,
                   source::CrashReportingStore* crashStore, NotificationQueue* notifications,
                   QWidget* parent = nullptr);

  signals:
    // Per-device dead-zone / motion page (Workstream 2d).
    void deadzonesRequested();
    // Setup wizard / help (Workstream 3a).
    void setupWizardRequested();
    void helpRequested();
    // Licenses (3c) / donate (3b).
    void licensesRequested();
    void donateRequested();
};

} // namespace dish::ui
