// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/FramelessWindowChrome.h"

#include <QWindow>

#include <cstring>

#include <windows.h>
#include <dwmapi.h>
#include <windowsx.h>

// Older SDK headers predate these, and the build must not hinge on which
// Windows SDK is installed.
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
    // Gate on handle(), the non-creating accessor. winId() re-creates a missing
    // platform window, and messages still pump after the window is destroyed at
    // shutdown: CreateWindowEx fails that late, so Qt trips
    // Q_ASSERT(platformWindow) in debug and dereferences null in release.
    if (window == nullptr || window->handle() == nullptr) { return nullptr; }
    return reinterpret_cast<HWND>(window->winId());
}

// GetVersionEx caps at 6.2 without a compatibility manifest and
// VerifyVersionInfo is shimmed, so only RtlGetVersion answers the Win11 gate.
unsigned long osBuildNumber() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) { return 0; }
    FARPROC proc = ::GetProcAddress(ntdll, "RtlGetVersion");
    if (proc == nullptr) { return 0; }
    // Copy the bits rather than cast them: a direct function-pointer cast trips
    // -Wcast-function-type-mismatch and laundering through void* trips
    // bugprone-casting-through-void. The explicit void* operands keep memcpy
    // from converting on its own; the static_assert makes a width mismatch a
    // compile error instead of a half-copied pointer.
    static_assert(sizeof(RtlGetVersionFn) == sizeof(FARPROC),
                  "RtlGetVersion and FARPROC must be the same width to copy between them");
    RtlGetVersionFn fn = nullptr;
    std::memcpy(static_cast<void*>(&fn), static_cast<const void*>(&proc), sizeof(fn));
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) == 0) { return info.dwBuildNumber; }
    return 0;
}

// The native hit-test works in device pixels; QML publishes logical ones.
Rect toPhysical(const QRect& logical, qreal dpr) {
    return Rect{static_cast<int>(logical.left() * dpr), static_cast<int>(logical.top() * dpr),
                static_cast<int>(logical.right() * dpr) + 1,
                static_cast<int>(logical.bottom() * dpr) + 1};
}

} // namespace

FramelessWindowChrome::FramelessWindowChrome(QWindow* window, QObject* parent)
    : QObject(parent), m_window(window) {
    // DefWindowProc runs the native sizing loop for the HTLEFT..HTBOTTOMRIGHT
    // answers below only when the window carries WS_THICKFRAME (the sizebox
    // style). Qt's FramelessWindowHint creates a bare WS_POPUP without it, so
    // an edge drag was answered with a resize code yet started nothing — on
    // every window this chrome drives. The bit adds no visuals: WM_NCCALCSIZE
    // returning 0 keeps the whole surface client area, and a fixed window
    // (minimum == maximum) stays fixed regardless, because Qt clamps the
    // track size through WM_GETMINMAXINFO. Both consumers construct this
    // filter after the platform window exists, so the handle is available.
    if (HWND hwnd = hwndOf(window)) {
        const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
        if ((style & WS_THICKFRAME) == 0) {
            ::SetWindowLongPtrW(hwnd, GWL_STYLE, style | WS_THICKFRAME);
            ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                           SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                               SWP_FRAMECHANGED);
        }
    }
}

FramelessWindowChrome::~FramelessWindowChrome() = default;

void FramelessWindowChrome::setCaptionRect(const QRect& rect) { m_captionRect = rect; }

void FramelessWindowChrome::setMaximizeButtonRect(const QRect& rect) {
    m_maximizeButtonRect = rect;
}

void FramelessWindowChrome::setMinimizeButtonRect(const QRect& rect) {
    m_minimizeButtonRect = rect;
}

void FramelessWindowChrome::setCloseButtonRect(const QRect& rect) { m_closeButtonRect = rect; }

void FramelessWindowChrome::setLeftClientRect(const QRect& rect) { m_leftClientRect = rect; }

void FramelessWindowChrome::setUpdatePillRect(const QRect& rect) { m_updatePillRect = rect; }

bool FramelessWindowChrome::applyMicaBackdrop() {
    HWND hwnd = hwndOf(m_window);
    if (hwnd == nullptr) { return false; }
    if (!isWin11OrLater(osBuildNumber())) {
        // The attribute ids are unsupported (and immersive-dark had a different,
        // reserved id) on early builds. QML paints a solid themed background.
        return false;
    }

    // setImmersiveDarkMode flips this later without re-applying Mica.
    const BOOL dark = TRUE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

    const int backdrop = DWMSBT_MAINWINDOW;
    ::DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

    // 1 px is enough for DWM to draw the shadow and resize border on a window
    // whose non-client area WM_NCCALCSIZE zeroes. A -1 sheet-of-glass margin
    // would instead let Mica bleed under content painted opaque.
    MARGINS margins{0, 0, 1, 0};
    ::DwmExtendFrameIntoClientArea(hwnd, &margins);

    return true;
}

void FramelessWindowChrome::setMaximizeButtonHovered(bool hovered) {
    if (m_maximizeButtonHovered == hovered) { return; }
    m_maximizeButtonHovered = hovered;
    emit maximizeButtonHoveredChanged(hovered);
}

void FramelessWindowChrome::setImmersiveDarkMode(bool dark) {
    HWND hwnd = hwndOf(m_window);
    if (hwnd == nullptr || !isWin11OrLater(osBuildNumber())) { return; }
    const BOOL value = dark ? TRUE : FALSE;
    ::DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value));
}

bool FramelessWindowChrome::nativeEventFilter(const QByteArray& eventType, void* message,
                                              qintptr* result) {
    if (eventType != QByteArrayLiteral("windows_generic_MSG")) { return false; }
    auto* msg = static_cast<MSG*>(message);
    HWND target = hwndOf(m_window);
    if (target == nullptr || msg->hwnd != target) { return false; }

    // Qt runs this filter from two places and only one carries a reply. The
    // platform plugin's window procedure passes a real `result`, but
    // QEventDispatcherWin32::processEvents offers every POSTED message to the
    // filter first with result == nullptr, where returning true simply drops the
    // message. Writing through the reply unconditionally is an access violation
    // on that path, and it is the path every mouse message takes. WM_NCHITTEST
    // and WM_NCCALCSIZE are sent, which is why they never meet it.
    const auto reply = [result](qintptr value) {
        if (result != nullptr) { *result = value; }
    };
    // An arm whose job is the answer rather than the swallow has nothing to say
    // on the pre-dispatch pass; let it reach the window procedure instead.
    const bool canAnswer = result != nullptr;

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (!canAnswer) { return false; }
        // 0 with wParam==TRUE means the whole window is client area, so the OS
        // draws no title bar while the extended frame keeps shadow and snap. The
        // proposed client rect must not be inset, or a maximized window clips
        // its top edge.
        if (msg->wParam == TRUE) {
            *result = 0;
            return true;
        }
        return false;
    }
    case WM_NCHITTEST: {
        if (!canAnswer) { return false; }
        RECT rc{};
        ::GetWindowRect(target, &rc);

        const int gx = GET_X_LPARAM(msg->lParam);
        const int gy = GET_Y_LPARAM(msg->lParam);

        const qreal dpr = m_window != nullptr ? m_window->devicePixelRatio() : 1.0;

        WINDOWPLACEMENT wp{sizeof(wp), 0, 0, {}, {}, {}};
        ::GetWindowPlacement(target, &wp);
        const bool maximized = wp.showCmd == SW_SHOWMAXIMIZED;

        HitTestInput in{};
        in.cursor = Point{gx - rc.left, gy - rc.top};
        in.window = Rect{0, 0, rc.right - rc.left, rc.bottom - rc.top};
        in.caption = toPhysical(m_captionRect, dpr);
        in.maximizeButton = toPhysical(m_maximizeButtonRect, dpr);
        in.minimizeButton = toPhysical(m_minimizeButtonRect, dpr);
        in.closeButton = toPhysical(m_closeButtonRect, dpr);
        in.leftClient = toPhysical(m_leftClientRect, dpr);
        in.updatePill = toPhysical(m_updatePillRect, dpr);
        // 8 logical px grab band, matching the Win11 default resize frame.
        in.resizeBorder = static_cast<int>(8 * dpr);
        in.maximized = maximized;

        switch (hitTest(in)) {
        case HitRegion::Caption:
            *result = HTCAPTION;
            break;
        // Only HTMAXBUTTON pops the Win11 Snap Layouts flyout over a custom
        // maximize button; HTCLIENT and plain HTCAPTION never will.
        case HitRegion::MaximizeButton:
            *result = HTMAXBUTTON;
            break;
        case HitRegion::Left:
            *result = HTLEFT;
            break;
        case HitRegion::Right:
            *result = HTRIGHT;
            break;
        case HitRegion::Top:
            *result = HTTOP;
            break;
        case HitRegion::Bottom:
            *result = HTBOTTOM;
            break;
        case HitRegion::TopLeft:
            *result = HTTOPLEFT;
            break;
        case HitRegion::TopRight:
            *result = HTTOPRIGHT;
            break;
        case HitRegion::BottomLeft:
            *result = HTBOTTOMLEFT;
            break;
        case HitRegion::BottomRight:
            *result = HTBOTTOMRIGHT;
            break;
        case HitRegion::Client:
            *result = HTCLIENT;
            break;
        }
        return true;
    }
    // Answering HTMAXBUTTON above costs the button its press for everyone else:
    // Quick sees no non-client mouse events, and DefWindowProc's caption-button
    // tracking needs a WS_CAPTION a frameless window does not have. So the whole
    // press runs here, per the Win11 custom-title-bar contract: swallow the
    // down, act on the up. m_maximizeButtonPressed is what makes a press that
    // wanders off and releases elsewhere do nothing.
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        if (msg->wParam != HTMAXBUTTON) {
            m_maximizeButtonPressed = false;
            return false; // HTCAPTION drag, resize edges and system menu are theirs
        }
        // A double-click is a second press: two toggles ending where it began,
        // which is what a native caption button does.
        m_maximizeButtonPressed = true;
        reply(0);
        return true;
    }
    case WM_NCLBUTTONUP: {
        if (msg->wParam != HTMAXBUTTON || !m_maximizeButtonPressed) {
            m_maximizeButtonPressed = false;
            return false;
        }
        m_maximizeButtonPressed = false;
        WINDOWPLACEMENT wp{sizeof(wp), 0, 0, {}, {}, {}};
        ::GetWindowPlacement(target, &wp);
        const WPARAM command = wp.showCmd == SW_SHOWMAXIMIZED ? SC_RESTORE : SC_MAXIMIZE;
        // Posted, not sent: SC_MAXIMIZE re-enters the window procedure and this
        // filter for the resize, which must not happen inside the up.
        ::PostMessageW(target, WM_SYSCOMMAND, command, 0);
        reply(0);
        return true;
    }
    case WM_NCMOUSEMOVE: {
        const bool over = msg->wParam == HTMAXBUTTON;
        if (over && !m_maximizeButtonHovered) {
            // Non-client moves stop the moment the cursor leaves, so without
            // this the fill sticks on whatever edge it left by.
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, target, 0};
            ::TrackMouseEvent(&track);
        }
        setMaximizeButtonHovered(over);
        return false;
    }
    case WM_NCMOUSELEAVE:
    case WM_MOUSEMOVE: // crossed into the client area
        setMaximizeButtonHovered(false);
        return false;
    case WM_LBUTTONUP:
        // Released in the client area after pressing the button: not a click.
        m_maximizeButtonPressed = false;
        return false;
    default:
        return false;
    }
}

} // namespace dish::chrome
