// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/ChromeBridge.h"

#include "qml/chrome/FramelessWindowChrome.h"

namespace dish::chrome {

ChromeBridge::ChromeBridge(QObject* parent) : QObject(parent) {}

void ChromeBridge::setMicaActive(bool active) {
    if (m_micaActive == active) { return; }
    m_micaActive = active;
    emit micaActiveChanged();
}

void ChromeBridge::setDark(bool dark) {
    if (m_dark == dark) { return; }
    m_dark = dark;
    emit darkChanged();
}

void ChromeBridge::setChrome(FramelessWindowChrome* chrome) {
    m_chrome = chrome;
    if (m_chrome == nullptr) { return; }
    // The filter owns the native hover of the maximize button (it is the only
    // party that sees WM_NCMOUSEMOVE); this just republishes it to QML.
    QObject::connect(m_chrome, &FramelessWindowChrome::maximizeButtonHoveredChanged, this,
                     [this](bool hovered) {
                         if (m_maximizeHovered == hovered) { return; }
                         m_maximizeHovered = hovered;
                         emit maximizeHoveredChanged();
                     });
    // Flush what QML published before the native window existed.
    m_chrome->setCaptionRect(m_caption);
    m_chrome->setMaximizeButtonRect(m_maximize);
    m_chrome->setMinimizeButtonRect(m_minimize);
    m_chrome->setCloseButtonRect(m_close);
    m_chrome->setLeftClientRect(m_leftClient);
}

void ChromeBridge::setCaptionRect(const QRect& rect) {
    m_caption = rect;
    if (m_chrome != nullptr) { m_chrome->setCaptionRect(rect); }
}

void ChromeBridge::setMaximizeButtonRect(const QRect& rect) {
    m_maximize = rect;
    if (m_chrome != nullptr) { m_chrome->setMaximizeButtonRect(rect); }
}

void ChromeBridge::setMinimizeButtonRect(const QRect& rect) {
    m_minimize = rect;
    if (m_chrome != nullptr) { m_chrome->setMinimizeButtonRect(rect); }
}

void ChromeBridge::setCloseButtonRect(const QRect& rect) {
    m_close = rect;
    if (m_chrome != nullptr) { m_chrome->setCloseButtonRect(rect); }
}

void ChromeBridge::setLeftClientRect(const QRect& rect) {
    m_leftClient = rect;
    if (m_chrome != nullptr) { m_chrome->setLeftClientRect(rect); }
}

} // namespace dish::chrome
