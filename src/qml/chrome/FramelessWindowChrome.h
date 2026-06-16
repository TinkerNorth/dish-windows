// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Win32 glue that turns a Qt Quick FramelessWindowHint ApplicationWindow
// into a real Windows 11 chrome: native WM_NCCALCSIZE / WM_NCHITTEST handling
// for Snap Layouts + resize borders, DwmExtendFrameIntoClientArea for the drop
// shadow, and the Mica backdrop. All side-effecting Win32 lives here; the
// region math is delegated to the pure dish::chrome::hitTest (WindowHitTest.h).
//
// This header is Qt/Quick-only and compiled exclusively in the DISH_QML build;
// the Widgets app never sees it.

#pragma once

#include "qml/chrome/WindowHitTest.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QRect>

#include <QtGlobal>

QT_BEGIN_NAMESPACE
class QWindow;
QT_END_NAMESPACE

namespace dish::chrome {

// Installs itself as the application native event filter and drives the chrome
// for a single top-level QWindow. The QML side reports the caption strip and
// maximize-button rects (in device-independent pixels) via setCaptionRect /
// setMaximizeButtonRect; this filter scales them to physical pixels for the
// hit-test. Lifetime: owned by the caller (main.cpp), outliving the window.
class FramelessWindowChrome : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit FramelessWindowChrome(QWindow* window, QObject* parent = nullptr);
    ~FramelessWindowChrome() override;

    // Apply Mica + immersive dark mode + the extended frame to the native HWND.
    // No-op (and returns false) on pre-Win11 so the caller can paint a solid
    // themed fallback instead. Safe to call once the window has a native handle.
    bool applyMicaBackdrop();

    // Flip the DWMWA_USE_IMMERSIVE_DARK_MODE attribute so the OS-drawn frame edges
    // / shadow tint match the app's resolved appearance. Called when the theme
    // mode changes so the native chrome doesn't drift light while the body re-darks
    // (the "Mica resolved light" mismatch). No-op on pre-Win11.
    void setImmersiveDarkMode(bool dark);

    bool nativeEventFilter(const QByteArray& eventType, void* message,
                           qintptr* result) override;

public slots:
    // The QML title bar publishes its geometry (logical px, window-local) here.
    void setCaptionRect(const QRect& rect);
    void setMaximizeButtonRect(const QRect& rect);

private:
    QWindow* m_window = nullptr;
    QRect m_captionRect;        // logical px
    QRect m_maximizeButtonRect; // logical px
};

} // namespace dish::chrome
