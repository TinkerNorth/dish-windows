// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/FramelessWindowChrome.h"

#include <QWindow>

#include <cstring>

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
    // handle() is the NON-CREATING accessor. winId() CREATES the platform window
    // when it is missing — and this filter is asked for the HWND on every native
    // message the app receives, including the ones still pumping AFTER the
    // window has been destroyed at shutdown. Resurrecting it there means asking
    // the platform integration to CreateWindowEx while the app tears down: it
    // fails ("WindowCreationData::create: CreateWindowEx failed"),
    // createPlatformWindow returns null, and Qt trips Q_ASSERT(platformWindow)
    // in QWindowPrivate::create — a debug abort on every clean exit, and a null
    // dereference in release. Ask, never conjure.
    if (window == nullptr || window->handle() == nullptr) { return nullptr; }
    return reinterpret_cast<HWND>(window->winId());
}

// Read the OS build number via RtlGetVersion. GetVersionEx lies (it caps at
// 6.2 unless the app manifests compatibility), and VerifyVersionInfo is
// shimmed; RtlGetVersion in ntdll returns the true build, which is what the
// Win11 gate needs.
unsigned long osBuildNumber() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (ntdll == nullptr) { return 0; }
    FARPROC proc = ::GetProcAddress(ntdll, "RtlGetVersion");
    if (proc == nullptr) { return 0; }
    // Copy the bits rather than cast them. FARPROC's signature is unrelated to
    // RtlGetVersion's, so a direct function-pointer cast trips
    // -Wcast-function-type-mismatch while laundering it through void* trips
    // bugprone-casting-through-void — with no cast between the two types, neither
    // has anything to match. memcpy's operands are spelled as explicit void* /
    // const void* so the call performs no implicit pointer conversion of its own
    // (bugprone-bitwise-pointer-cast, -multi-level-implicit-pointer-conversion).
    // Both are function pointers, so the widths agree on every Windows ABI; the
    // static_assert makes a mismatch a compile error, not a half-copied pointer.
    static_assert(sizeof(RtlGetVersionFn) == sizeof(FARPROC),
                  "RtlGetVersion and FARPROC must be the same width to copy between them");
    RtlGetVersionFn fn = nullptr;
    std::memcpy(static_cast<void*>(&fn), static_cast<const void*>(&proc), sizeof(fn));
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (fn(&info) == 0) { return info.dwBuildNumber; }
    return 0;
}

// Scale a logical-pixel window-local rect to physical pixels for the native
// hit-test, which works in device pixels.
Rect toPhysical(const QRect& logical, qreal dpr) {
    return Rect{static_cast<int>(logical.left() * dpr), static_cast<int>(logical.top() * dpr),
                static_cast<int>(logical.right() * dpr) + 1,
                static_cast<int>(logical.bottom() * dpr) + 1};
}

} // namespace

FramelessWindowChrome::FramelessWindowChrome(QWindow* window, QObject* parent)
    : QObject(parent), m_window(window) {}

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

bool FramelessWindowChrome::applyMicaBackdrop() {
    HWND hwnd = hwndOf(m_window);
    if (hwnd == nullptr) { return false; }
    if (!isWin11OrLater(osBuildNumber())) {
        // Pre-Win11: the backdrop + dark-mode attributes are unsupported and the
        // immersive-dark attribute had a different (reserved) id on early builds.
        // Leave the window opaque; the QML paints a solid themed background.
        return false;
    }

    // Default to the deep-space dark frame (the app's design default). A later
    // theme change flips this via setImmersiveDarkMode without re-applying Mica.
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

    // Qt runs this filter from TWO places and only one of them can carry a
    // reply. The platform plugin's window procedure passes a real `result`; but
    // QEventDispatcherWin32::processEvents also offers every POSTED message to
    // the filter BEFORE dispatching it, with result == nullptr — returning true
    // there just drops the message instead of TranslateMessage/DispatchMessage,
    // which is exactly the "swallow it" semantics the button press wants.
    // Writing through the reply without checking is an access violation on that
    // path, and it is the path every mouse message takes (WM_NCHITTEST and
    // WM_NCCALCSIZE are SENT, which is why they never met it).
    const auto reply = [result](qintptr value) {
        if (result != nullptr) { *result = value; }
    };
    // An arm whose whole job is the ANSWER (not the swallow) has nothing to say
    // on the pre-dispatch pass; let the message continue to the window
    // procedure, where the filter runs again with somewhere to write.
    const bool canAnswer = result != nullptr;

    switch (msg->message) {
    case WM_NCCALCSIZE: {
        if (!canAnswer) { return false; }
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
        // 8 logical px grab band, matching the Win11 default resize frame feel.
        in.resizeBorder = static_cast<int>(8 * dpr);
        in.maximized = maximized;

        switch (hitTest(in)) {
        case HitRegion::Caption:
            *result = HTCAPTION;
            break;
        // HTMAXBUTTON is what makes Win11 pop the Snap Layouts flyout over a
        // custom maximize button — HTCLIENT or a plain HTCAPTION never will.
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
    // ── The maximize button's whole event life, because nothing else has it ──
    //
    // WM_NCHITTEST above answers HTMAXBUTTON over that rect — the only way to
    // get the Win11 Snap Layouts flyout on hover. The cost is that the button
    // stops existing for everyone else:
    //
    //   * Quick never sees the press. Non-client mouse events reach a QWindow
    //     only when frame-strut events are enabled, which Quick does not do, so
    //     WindowTitleBar's onClicked is unreachable and the item never hovers.
    //   * DefWindowProc never acts on it either. Its caption-button tracking
    //     (NC_TrackMinMaxBox) is driven by the CAPTION, and a frameless window
    //     has no WS_CAPTION — the measured style is WS_POPUP | WS_SYSMENU |
    //     WS_MINIMIZEBOX | WS_MAXIMIZEBOX. WS_MAXIMIZEBOX is what makes the
    //     caption DOUBLE-click (HTCAPTION, which DefWindowProc does handle)
    //     zoom the window, but no amount of style bits makes it track a button
    //     on a window that draws no caption.
    //
    // So the press is ours to run, exactly as the Win11 custom-title-bar
    // contract requires: swallow the down, act on the up. Press-and-release
    // must land on the SAME button, which is what m_maximizeButtonPressed
    // records — a press that wanders off and releases elsewhere does nothing.
    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
        if (msg->wParam != HTMAXBUTTON) {
            m_maximizeButtonPressed = false;
            return false; // HTCAPTION drag, resize edges, system menu — theirs
        }
        // A double-click is a second press: two toggles, ending where it began,
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
        // POSTed, not sent: SC_MAXIMIZE re-enters the window procedure (and this
        // very filter) for the resize, which must not happen inside the up.
        ::PostMessageW(target, WM_SYSCOMMAND, command, 0);
        reply(0);
        return true;
    }
    case WM_NCMOUSEMOVE: {
        const bool over = msg->wParam == HTMAXBUTTON;
        if (over && !m_maximizeButtonHovered) {
            // Non-client moves stop arriving the moment the cursor leaves, so
            // without this the fill would stick on whatever edge it left by.
            TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, target, 0};
            ::TrackMouseEvent(&track);
        }
        setMaximizeButtonHovered(over);
        return false; // still Qt's / DefWindowProc's message
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
