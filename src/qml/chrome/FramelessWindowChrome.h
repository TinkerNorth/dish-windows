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
#include <QPointer>
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

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

  signals:
    // The maximize button is the ONE caption button that must stay NON-CLIENT:
    // Snap Layouts opens only over a region that answers HTMAXBUTTON. The price
    // is that Qt delivers neither hover nor click to the QML item under it (a
    // non-client mouse event reaches a Quick window only through frame-strut
    // events, which Quick never enables), so the button reads as dead. The
    // filter reconstructs the hover from the native messages and hands it back
    // here; WindowTitleBar paints the fill off it.
    void maximizeButtonHoveredChanged(bool hovered);

  public slots:
    // The QML title bar publishes its geometry (logical px, window-local) here.
    void setCaptionRect(const QRect& rect);
    void setMaximizeButtonRect(const QRect& rect);
    // Client carve-outs INSIDE the caption strip: without them the native
    // resolver answers HTCAPTION over the hamburger / minimize / close and a
    // press starts a system drag instead of reaching the QML buttons.
    void setMinimizeButtonRect(const QRect& rect);
    void setCloseButtonRect(const QRect& rect);
    void setLeftClientRect(const QRect& rect);

  private:
    // Distinct-until-changed, so a stream of WM_NCMOUSEMOVE over the button
    // emits once.
    void setMaximizeButtonHovered(bool hovered);

    // OWNED BY THE ENGINE, AND IT DIES FIRST. This filter is parented to the app
    // precisely so it can outlive the window (see QmlEntryPoint), and it is an
    // APPLICATION-wide native event filter — it is handed every message the
    // process receives, including the ones that still pump while the QML engine
    // is tearing the window down. A raw pointer is dangling for all of those.
    // QPointer nulls itself the moment the window is destroyed, which turns
    // every arm below into an early `return false`.
    QPointer<QWindow> m_window;
    QRect m_captionRect;        // logical px
    QRect m_maximizeButtonRect; // logical px
    QRect m_minimizeButtonRect; // logical px (client carve-out)
    QRect m_closeButtonRect;    // logical px (client carve-out)
    QRect m_leftClientRect;     // logical px (client carve-out — hamburger)
    // Native hover/press state for the maximize button — see the signal above
    // for why it cannot come from Quick.
    bool m_maximizeButtonHovered = false;
    bool m_maximizeButtonPressed = false;
};

} // namespace dish::chrome
