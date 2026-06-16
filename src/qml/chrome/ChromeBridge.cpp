// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/ChromeBridge.h"

#include "qml/chrome/FramelessWindowChrome.h"

namespace dish::chrome {

ChromeBridge::ChromeBridge(QObject* parent) : QObject(parent) {}

void ChromeBridge::setMicaActive(bool active) {
    if (m_micaActive == active) {
        return;
    }
    m_micaActive = active;
    emit micaActiveChanged();
}

void ChromeBridge::setCaptionRect(const QRect& rect) {
    if (m_chrome) {
        m_chrome->setCaptionRect(rect);
    }
}

void ChromeBridge::setMaximizeButtonRect(const QRect& rect) {
    if (m_chrome) {
        m_chrome->setMaximizeButtonRect(rect);
    }
}

} // namespace dish::chrome
