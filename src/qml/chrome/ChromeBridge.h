// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The QML-facing singleton that bridges the QML title bar to the C++ chrome
// filter: QML publishes the caption + maximize-button rects here and reads
// micaActive (so it can pick a transparent vs solid background). main.cpp wires
// this to the FramelessWindowChrome instance after the window has a handle.
//
// QML/Quick-only; DISH_QML build exclusively.

#pragma once

#include <QObject>
#include <QRect>
#include <QtQml/qqmlregistration.h>

namespace dish::chrome {

class FramelessWindowChrome;

class ChromeBridge : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool micaActive READ micaActive NOTIFY micaActiveChanged)

public:
    explicit ChromeBridge(QObject* parent = nullptr);

    bool micaActive() const { return m_micaActive; }
    void setMicaActive(bool active);

    // Wire to the native chrome filter. The bridge forwards rect updates to it.
    void setChrome(FramelessWindowChrome* chrome) { m_chrome = chrome; }

    Q_INVOKABLE void setCaptionRect(const QRect& rect);
    Q_INVOKABLE void setMaximizeButtonRect(const QRect& rect);

signals:
    void micaActiveChanged();

private:
    FramelessWindowChrome* m_chrome = nullptr;
    bool m_micaActive = false;
};

} // namespace dish::chrome
