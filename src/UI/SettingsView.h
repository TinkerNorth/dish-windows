// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "source/store/ThemePreferenceStore.h"

#include <QWidget>

class QButtonGroup;
class QComboBox;

namespace dish {
class FeatureSettings;
}

namespace dish::source {
class CrashReportingStore;
}

namespace dish::ui {

class NotificationQueue;

// Feature-forwarding + app preferences surface. A self-contained QWidget so the
// two desktop shells can host it however they like — dish-windows wraps it in a
// modal QDialog — while the settings controls stay one shared implementation.
//
// Workstream 3d grows this from "lightbar only" to the full android settings
// surface: Setup & Help (links to the setup wizard + help/FAQ), Appearance (a
// Light / Dark / System theme picker bound to the ThemePreferenceStore),
// Diagnostics (the crash-reporting toggle row, OWNED + supplied by Workstream
// 3e — this view only reserves the slot and inserts the factory's widget), and
// About (privacy policy, open-source licenses, support/donate, version). The
// motion-enable / deadzone tuning entry (Workstream 2d) keeps its row.
class SettingsView : public QWidget {
    Q_OBJECT
  public:
    // All dependencies are owned by the AppModel / composition root and outlive
    // this view. `themeStore` drives the Appearance picker; `crashStore` (may be
    // null) backs the Diagnostics toggle; `notifications` (may be null) receives
    // external-url-failed warnings from the About links.
    SettingsView(FeatureSettings* settings, source::ThemePreferenceStore* themeStore,
                 source::CrashReportingStore* crashStore, NotificationQueue* notifications,
                 QWidget* parent = nullptr);

  signals:
    // Emitted when the user dismisses the view (the Done button). The host shell
    // closes the dialog / pops the page.
    void closeRequested();

    // Per-device dead-zone / motion page request (Workstream 2d). The host
    // presents DeadzoneSettingsView.
    void deadzonesRequested();

    // Setup & Help section requests (Workstream 3a). The host opens the setup
    // wizard / help screen.
    void setupWizardRequested();
    void helpRequested();

    // About section requests. The host opens the licenses (3c) / donate (3b)
    // screens.
    void licensesRequested();
    void donateRequested();

  private:
    void onLightbarModeChanged(int index);
    void onThemeChipClicked(int modeValue);

    FeatureSettings* settings_;
    source::ThemePreferenceStore* themeStore_;
    source::CrashReportingStore* crashStore_;
    NotificationQueue* notifications_;
    QComboBox* lightbarCombo_;
    QButtonGroup* themeGroup_;
};

} // namespace dish::ui
