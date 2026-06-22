// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// HelpView — the help / FAQ screen (Workstream 3a). Mirrors dish-android
// ui/onboarding/HelpActivity.kt: collapsible FAQ rows grouped into sections
// (Concepts / Performance / Troubleshooting / About) + external-link cards
// (privacy policy, GitHub) + a "Run setup" card that relaunches the adapted
// setup wizard.
//
// Adapted for a Windows physical-controller client: the phone/BT/USB-claim-
// specific FAQ rows are DROPPED (android's help_q_lan_vs_bluetooth and the
// on-screen/USB-direct/BT-mode controller-modes row); the satellite / motion /
// touchpad / disconnect / privacy / open-source rows that apply on Windows are
// kept (some reworded for the controller-into-PC framing).
//
// Collapsible pattern: a checkable QToolButton header reveals a body label (no
// chevron animation required, Theme-tinted).

#pragma once

#include <QDialog>

namespace dish::ui {

class NotificationQueue;

class HelpView : public QDialog {
    Q_OBJECT
  public:
    // `notifications` (borrowed, may be null) receives external-url-failed.
    explicit HelpView(NotificationQueue* notifications, QWidget* parent = nullptr);

  signals:
    // The "Run setup" card was tapped: the host opens the setup wizard.
    void runSetupRequested();

  private:
    NotificationQueue* notifications_;
};

} // namespace dish::ui
