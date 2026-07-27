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
    // The resolved app appearance. The body is transparent (Mica shows) only when
    // dark — Mica's tint follows the OS, so a light app over a dark desktop would
    // otherwise keep a dark backdrop while the cards/text go light. In light mode
    // we paint the solid light Theme.background instead.
    Q_PROPERTY(bool dark READ dark NOTIFY darkChanged)

  public:
    explicit ChromeBridge(QObject* parent = nullptr);

    bool micaActive() const { return m_micaActive; }
    void setMicaActive(bool active);
    bool dark() const { return m_dark; }
    void setDark(bool dark);

    // Wire to the native chrome filter. Rects published BEFORE this (the QML
    // title bar completes before the native window exists) are cached and
    // flushed here, so the first frames already hit-test correctly.
    void setChrome(FramelessWindowChrome* chrome);

    Q_INVOKABLE void setCaptionRect(const QRect& rect);
    Q_INVOKABLE void setMaximizeButtonRect(const QRect& rect);
    // Client carve-outs inside the caption strip (hamburger / minimize /
    // close) so those QML buttons receive real clicks instead of the press
    // starting a native caption drag.
    Q_INVOKABLE void setMinimizeButtonRect(const QRect& rect);
    Q_INVOKABLE void setCloseButtonRect(const QRect& rect);
    Q_INVOKABLE void setLeftClientRect(const QRect& rect);

  signals:
    void micaActiveChanged();
    void darkChanged();

  private:
    FramelessWindowChrome* m_chrome = nullptr;
    bool m_micaActive = false;
    bool m_dark = true; // app defaults to the dark palette
    // Last published rects, cached so a late-wired chrome starts correct.
    QRect m_caption;
    QRect m_maximize;
    QRect m_minimize;
    QRect m_close;
    QRect m_leftClient;
};

} // namespace dish::chrome
