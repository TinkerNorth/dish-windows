// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The desktop notification, as the tray item's own balloon: on Windows 10 and
// 11 the shell renders it as a toast, it needs no COM identity or Start-menu
// shortcut the portable build could not provide, and it is anchored to the
// item the notice tells the user to look for.

#pragma once

#include "source/notification/DesktopNotifier.h"

namespace dish::source {

class Win32TrayIcon;

class TrayBalloonNotifier final : public DesktopNotifier {
    Q_OBJECT
  public:
    // `tray` is borrowed; nullptr makes notify() a no-op.
    explicit TrayBalloonNotifier(Win32TrayIcon* tray, QObject* parent = nullptr);

    void notify(const QString& summary, const QString& body) override;

  private:
    Win32TrayIcon* tray_;
};

} // namespace dish::source
