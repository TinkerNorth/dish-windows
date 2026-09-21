// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/tray/TrayBalloonNotifier.h"

#include "source/tray/Win32TrayIcon.h"

namespace dish::source {

TrayBalloonNotifier::TrayBalloonNotifier(Win32TrayIcon* tray, QObject* parent)
    : DesktopNotifier(parent), tray_(tray) {}

void TrayBalloonNotifier::notify(const QString& summary, const QString& body) {
    if (tray_ == nullptr) { return; }
    tray_->showBalloon(summary, body);
}

} // namespace dish::source
