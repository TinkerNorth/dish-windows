// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The QML title bar's seam onto the native chrome filter: QML publishes its
// rects here and reads back what only Win32 knows. QmlEntryPoint wires the
// filter in once the window has a handle.

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
    // Gates a transparent (Mica) vs solid body. Mica's tint follows the OS, so a
    // light app over a dark desktop would otherwise keep a dark backdrop while
    // the cards and text go light.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)
    // The maximize button answers HTMAXBUTTON (the Snap Layouts contract), which
    // makes it NON-CLIENT, and Quick never receives non-client mouse events — so
    // the QML item's own `hovered` is permanently false. The filter reconstructs
    // hover from the native messages and reports it here.
    Q_PROPERTY(bool maximizeHovered READ maximizeHovered NOTIFY maximizeHoveredChanged)

  public:
    explicit ChromeBridge(QObject* parent = nullptr);

    bool micaActive() const { return m_micaActive; }
    void setMicaActive(bool active);
    bool dark() const { return m_dark; }
    void setDark(bool dark);
    bool maximizeHovered() const { return m_maximizeHovered; }

    // The QML title bar completes before the native window exists, so rects
    // published earlier are cached and flushed here — the first frames already
    // hit-test correctly.
    void setChrome(FramelessWindowChrome* chrome);

    Q_INVOKABLE void setCaptionRect(const QRect& rect);
    Q_INVOKABLE void setMaximizeButtonRect(const QRect& rect);
    // Client carve-outs inside the caption strip, so these QML buttons receive
    // real clicks instead of the press starting a native caption drag.
    Q_INVOKABLE void setMinimizeButtonRect(const QRect& rect);
    Q_INVOKABLE void setCloseButtonRect(const QRect& rect);
    Q_INVOKABLE void setLeftClientRect(const QRect& rect);

  signals:
    void micaActiveChanged();
    void darkChanged();
    void maximizeHoveredChanged();

  private:
    FramelessWindowChrome* m_chrome = nullptr;
    bool m_micaActive = false;
    bool m_dark = true; // app defaults to the dark palette
    bool m_maximizeHovered = false;
    // Cached so a late-wired chrome starts correct.
    QRect m_caption;
    QRect m_maximize;
    QRect m_minimize;
    QRect m_close;
    QRect m_leftClient;
};

} // namespace dish::chrome
