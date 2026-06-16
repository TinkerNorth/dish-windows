// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/FramelessWindowChrome.h"

#include <QWindow>

#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>

// Some SDK headers predate these; define defensively so the build doesn't hinge
// on the installed Windows SDK version.
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif
#ifndef DWMSBT_MAINWINDOW
#define DWMSBT_MAINWINDOW 2
#endif

namespace dish::chrome {

namespace {

HWND hwndOf(QWindow* window) {
    return window ? reinterpret_cast<HWND>(window->winId()) : nullptr;
}

// Read the OS build number via RtlGetVersion. GetVersionEx lies (it caps at
// 6.2 unless the app manifests compatibility), and VerifyVersionInfo is
// shimmed; RtlGetVersion in ntdll returns the true build, which is what the
// Win11 gate needs.
unsigned long osBuildNumber() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll")) {
        if (auto fn = reinterpret_cast<RtlGetVersionFn>(
                ::GetProcAddress(ntdll, "RtlGetVersion"))) {
            RTL_OSVERSIONINFOW info{};
            info.dwOSVersionInfoSize = sizeof(info);
            if (fn(&info) == 0) {
                return info.dwBuildNumber;
            }
        }
    }
    return 0;
}

// Scale a logical-pixel window-local rect to physical pixels for the native
// hit-test, which works in device pixels.
Rect toPhysical(const QRect& logical, qreal dpr) {
    return Rect{static_cast<int>(logical.left() * dpr),
                static_cast<int>(logical.top() * dpr),
                static_cast<int>(logical.right() * dpr) + 1,
                static_cast<int>(logical.bottom() * dpr) + 1};
}

} // namespace

FramelessWindowChrome::FramelessWindowChrome(QWindow* window, QObject* parent)
    : QObject(parent), m_window(window) {}

FramelessWindowChrome::~FramelessWindowChrome() = default;

void FramelessWindowChrome::setCaptionRect(const QRect& rect) {
    m_captionRect = rect;
}

void FramelessWindowChrome::setMaximizeButtonRect(const QRect& rect) {
    m_maximizeButtonRect = rect;
}

bool FramelessWindowChrome::applyMicaBackdrop() {
    HWND hwnd = hwndOf(m_window);
    if (!hwnd) {
        return false;
    }
    if (!isWin11OrLater(osBuildNumber())) {
        // Pre-Win11: the backdrop + dark-mode attributes are unsupported and the
        // immersive-dark attribute had a different (reserved) id on early builds.
        // Leave the window opaque; the QML paints a solid themed background.
        return false;
    }

    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const int backdrop = DWMSBT_MAINWINDOW;
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // A 1px extension is enough to make DWM render the native drop shadow +
    // resize border for a window that otherwise has its whole non-client area
    // zeroed by WM_NCCALCSIZE. A full -1 (sheet-of-glass) margin would let Mica
    // bleed under content we paint opaque; 1px keeps the shadow without that.
    MARGINS margins{0, 0, 1, 0};
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);

    return true;
}

bool FramelessWindowChrome::nativeEventFilter(const QByteArray& eventType,
                                              void* message, qintptr* result) {
    if (eventType != QByteArrayLiteral("windows_generic_MSG")) {
        return false;
    }
    auto* msg = static_cast<MSG*>(message);
    HWND target = hwndOf(m_window);
    if (!target || msg->hwnd != target) {
        return false;
    }

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        // Returning 0 with wParam==TRUE tells DWM the entire window is client
        // area — the OS draws no title bar / borders, but (because we extend the
        // frame) keeps the shadow + snap behaviour. We must NOT inset the
        // proposed client rect, or a maximized window would clip its top edge.
        if (msg->wParam == TRUE) {
            *result = 0;
            return true;
        }
        return false;
    }
    case WM_NCHITTEST: {
        RECT rc{};
        ::GetWindowRect(target, &rc);

        const int gx = GET_X_LPARAM(msg->lParam);
        const int gy = GET_Y_LPARAM(msg->lParam);

        const qreal dpr = m_window ? m_window->devicePixelRatio() : 1.0;

        WINDOWPLACEMENT wp{sizeof(wp), 0, 0, {}, {}, {}};
        ::GetWindowPlacement(target, &wp);
        const bool maximized = wp.showCmd == SW_SHOWMAXIMIZED;

        HitTestInput in{};
        in.cursor = Point{gx - rc.left, gy - rc.top};
        in.window = Rect{0, 0, rc.right - rc.left, rc.bottom - rc.top};
        in.caption = toPhysical(m_captionRect, dpr);
        in.maximizeButton = toPhysical(m_maximizeButtonRect, dpr);
        // 8 logical px grab band, matching the Win11 default resize frame feel.
        in.resizeBorder = static_cast<int>(8 * dpr);
        in.maximized = maximized;

        switch (hitTest(in)) {
        case HitRegion::Caption:        *result = HTCAPTION; break;
        // HTMAXBUTTON is what makes Win11 pop the Snap Layouts flyout over a
        // custom maximize button — HTCLIENT or a plain HTCAPTION never will.
        case HitRegion::MaximizeButton: *result = HTMAXBUTTON; break;
        case HitRegion::Left:           *result = HTLEFT; break;
        case HitRegion::Right:          *result = HTRIGHT; break;
        case HitRegion::Top:            *result = HTTOP; break;
        case HitRegion::Bottom:         *result = HTBOTTOM; break;
        case HitRegion::TopLeft:        *result = HTTOPLEFT; break;
        case HitRegion::TopRight:       *result = HTTOPRIGHT; break;
        case HitRegion::BottomLeft:     *result = HTBOTTOMLEFT; break;
        case HitRegion::BottomRight:    *result = HTBOTTOMRIGHT; break;
        case HitRegion::Client:         *result = HTCLIENT; break;
        }
        return true;
    }
    default:
        return false;
    }
}

} // namespace dish::chrome
