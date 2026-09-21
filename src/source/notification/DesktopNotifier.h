// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A desktop notification, for the one thing the in-window toast host cannot
// carry: the window is gone by the time this has to be read. Abstract so the
// close policy can be pinned with a recording fake; the Windows implementation
// is the tray item's own balloon (source/tray/TrayBalloonNotifier.h).

#pragma once

#include <QObject>
#include <QString>

namespace dish::source {

class DesktopNotifier : public QObject {
    Q_OBJECT
  public:
    explicit DesktopNotifier(QObject* parent = nullptr) : QObject(parent) {}
    ~DesktopNotifier() override = default;

    virtual void notify(const QString& summary, const QString& body) = 0;
};

} // namespace dish::source
