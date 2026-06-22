// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// LicensesView — the open-source-licenses screen (Workstream 3c). A thin
// renderer over the bundled LicenseManifest: one card per third-party dependency
// showing name / version / license (via the pure mapping rules), with
// interactive rows opening their license URL and non-interactive rows inert.
// Mirrors dish-android ui/settings/LicensesActivity.kt + LicensesAdapter.kt,
// built as a humble Qt dialog (the dish-windows shell idiom).

#pragma once

#include <QDialog>

namespace dish::ui {

class NotificationQueue;

class LicensesView : public QDialog {
    Q_OBJECT
  public:
    // `notifications` (borrowed, may be null) receives the external-url-failed
    // warning when a license link won't open.
    explicit LicensesView(NotificationQueue* notifications, QWidget* parent = nullptr);

  private:
    NotificationQueue* notifications_;
};

} // namespace dish::ui
