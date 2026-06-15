// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "SettingsDialog.h"

#include "SettingsView.h"

#include <QVBoxLayout>

namespace dish::ui {

SettingsDialog::SettingsDialog(FeatureSettings* settings, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Settings"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* view = new SettingsView(settings, this);
    // The view's Done button closes the modal sheet.
    QObject::connect(view, &SettingsView::closeRequested, this, &QDialog::accept);
    // Forward the dead-zone page request up to the host (MainWindow).
    QObject::connect(view, &SettingsView::deadzonesRequested, this,
                     &SettingsDialog::deadzonesRequested);
    layout->addWidget(view);
}

} // namespace dish::ui
