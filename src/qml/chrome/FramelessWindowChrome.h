// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Win32 glue that turns a FramelessWindowHint ApplicationWindow into a real
// Windows 11 chrome. All side-effecting Win32 lives here; the region math is
// delegated to the pure dish::chrome::hitTest (WindowHitTest.h).

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

// An application-wide native event filter driving the chrome of one top-level
// QWindow. QML publishes its rects in device-independent pixels; this filter
// scales them to physical pixels before hit-testing.
class FramelessWindowChrome : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
  public:
    explicit FramelessWindowChrome(QWindow* window, QObject* parent = nullptr);
    ~FramelessWindowChrome() override;

    // False on pre-Win11, where the caller has to paint a solid themed fallback.
    // Needs a native handle already created.
    bool applyMicaBackdrop();

    // Re-flip DWMWA_USE_IMMERSIVE_DARK_MODE on a theme change, or the OS-drawn
    // frame edges stay light while the body re-darks. No-op on pre-Win11.
    void setImmersiveDarkMode(bool dark);

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

  signals:
    // The maximize button is the one caption button that must stay non-client:
    // Snap Layouts opens only over a region answering HTMAXBUTTON. The price is
    // that Qt delivers neither hover nor click to the QML item under it, since a
    // non-client mouse event reaches a Quick window only through frame-strut
    // events and Quick never enables those. The filter reconstructs hover from
    // the native messages and hands it back here.
    void maximizeButtonHoveredChanged(bool hovered);

  public slots:
    // Logical px, window-local.
    void setCaptionRect(const QRect& rect);
    void setMaximizeButtonRect(const QRect& rect);
    // Client carve-outs inside the caption strip: without them the native
    // resolver answers HTCAPTION over the hamburger / minimize / close and a
    // press starts a system drag instead of reaching the QML buttons.
    void setMinimizeButtonRect(const QRect& rect);
    void setCloseButtonRect(const QRect& rect);
    void setLeftClientRect(const QRect& rect);
    // Empty whenever the pill is hidden, which is most of a build's life.
    void setUpdatePillRect(const QRect& rect);

  private:
    // Distinct-until-changed, so a stream of WM_NCMOUSEMOVE emits once.
    void setMaximizeButtonHovered(bool hovered);

    // Owned by the engine, and it dies first. This filter outlives the window on
    // purpose and still receives every message the process pumps during teardown,
    // so a raw pointer would dangle. QPointer nulls itself and every arm below
    // then returns false early.
    QPointer<QWindow> m_window;
    QRect m_captionRect;        // logical px
    QRect m_maximizeButtonRect; // logical px
    QRect m_minimizeButtonRect; // logical px (client carve-out)
    QRect m_closeButtonRect;    // logical px (client carve-out)
    QRect m_leftClientRect;     // logical px (client carve-out, hamburger)
    QRect m_updatePillRect;     // logical px (client carve-out, update pill)
    bool m_maximizeButtonHovered = false;
    bool m_maximizeButtonPressed = false;
};

} // namespace dish::chrome
