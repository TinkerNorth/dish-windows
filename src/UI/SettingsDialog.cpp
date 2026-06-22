// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "SettingsDialog.h"

#include "SettingsView.h"

#include <QVBoxLayout>

namespace dish::ui {

SettingsDialog::SettingsDialog(FeatureSettings* settings, source::ThemePreferenceStore* themeStore,
                               source::CrashReportingStore* crashStore,
                               NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* view = new SettingsView(settings, themeStore, crashStore, notifications, this);
    // The view's Done button closes the modal sheet.
    QObject::connect(view, &SettingsView::closeRequested, this, &QDialog::accept);
    // Forward the screen-open requests up to the host (MainWindow).
    QObject::connect(view, &SettingsView::deadzonesRequested, this,
                     &SettingsDialog::deadzonesRequested);
    QObject::connect(view, &SettingsView::setupWizardRequested, this,
                     &SettingsDialog::setupWizardRequested);
    QObject::connect(view, &SettingsView::helpRequested, this, &SettingsDialog::helpRequested);
    QObject::connect(view, &SettingsView::licensesRequested, this,
                     &SettingsDialog::licensesRequested);
    QObject::connect(view, &SettingsView::donateRequested, this, &SettingsDialog::donateRequested);
    layout->addWidget(view);
}

} // namespace dish::ui
