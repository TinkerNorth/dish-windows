// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PURE, side-effect-free frameless-window hit-test math, factored out of the
// native event filter so it is unit-testable without an HWND. The native
// WM_NCHITTEST handler converts the cursor to client coordinates, fills in the
// geometry below, calls hitTest(), and maps the returned region to a Win32 HT*
// constant. No Qt, no Win32 types here — plain ints — so the tests link nothing.

#pragma once

namespace dish::chrome {

// A point/rectangle in the same coordinate space (window-local, top-left
// origin, x to the right, y down). Rect is half-open on the high edges:
// [left, right) x [top, bottom), so a point at x == right is OUTSIDE — this is
// what makes the off-by-one at a button's right/bottom edge well-defined.
struct Point {
    int x;
    int y;
};

struct Rect {
    int left;
    int top;
    int right;
    int bottom;
};

// The hit region a cursor position resolves to. The eight resize regions map to
// the Win32 HTLEFT/HTTOPLEFT/... constants; Caption -> HTCAPTION (draggable);
// MaximizeButton -> HTMAXBUTTON, which is the ONLY way Win11 shows the Snap
// Layouts flyout for a custom-drawn maximize button on a frameless window;
// Client -> HTCLIENT (the QML scene handles the event normally).
enum class HitRegion {
    Client,
    Caption,
    MaximizeButton,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
};

// Inputs to a single hit test. `window` is the window's client rect in the same
// space as `cursor` (normally {0,0,width,height}). `caption` is the draggable
// title-bar strip; `maximizeButton` is the maximize control's rect (caption
// buttons sit inside the caption strip but must win over Caption so the system
// can detect the snap hover). `resizeBorder` is the grab thickness for the
// resize frame in the same units. When `maximized` is true the window has no
// resize frame (you can't resize a maximized window), so the border regions are
// suppressed and only Caption/MaximizeButton/Client are reported.
struct HitTestInput {
    Point cursor;
    Rect window;
    Rect caption;
    Rect maximizeButton;
    int resizeBorder;
    bool maximized;
};

// Pure decision. Order matters and is deliberate:
//   1) Resize corners/edges first (a 1px corner must beat the caption strip that
//      overlaps it at the very top), UNLESS maximized.
//   2) The maximize button (so Snap Layouts can trigger) before the caption.
//   3) The caption strip (draggable).
//   4) Client otherwise.
constexpr HitRegion hitTest(const HitTestInput& in) {
    const int x = in.cursor.x;
    const int y = in.cursor.y;

    const bool inside =
        x >= in.window.left && x < in.window.right && y >= in.window.top && y < in.window.bottom;
    if (!inside) { return HitRegion::Client; }

    if (!in.maximized && in.resizeBorder > 0) {
        const bool nearLeft = x < in.window.left + in.resizeBorder;
        const bool nearRight = x >= in.window.right - in.resizeBorder;
        const bool nearTop = y < in.window.top + in.resizeBorder;
        const bool nearBottom = y >= in.window.bottom - in.resizeBorder;

        if (nearTop && nearLeft) { return HitRegion::TopLeft; }
        if (nearTop && nearRight) { return HitRegion::TopRight; }
        if (nearBottom && nearLeft) { return HitRegion::BottomLeft; }
        if (nearBottom && nearRight) { return HitRegion::BottomRight; }
        if (nearLeft) { return HitRegion::Left; }
        if (nearRight) { return HitRegion::Right; }
        if (nearTop) { return HitRegion::Top; }
        if (nearBottom) { return HitRegion::Bottom; }
    }

    const bool inMaximize = x >= in.maximizeButton.left && x < in.maximizeButton.right &&
                            y >= in.maximizeButton.top && y < in.maximizeButton.bottom;
    if (inMaximize) { return HitRegion::MaximizeButton; }

    const bool inCaption = x >= in.caption.left && x < in.caption.right && y >= in.caption.top &&
                           y < in.caption.bottom;
    if (inCaption) { return HitRegion::Caption; }

    return HitRegion::Client;
}

// PURE Win11 predicate: Mica + immersive dark mode + the custom snap hit-test
// are only valid on Windows 11 (build >= 22000). On Windows 10 the DWM
// attributes used for those are unsupported/no-ops or misbehave, so the caller
// must fall back to a solid background and skip them. Build numbers below the
// threshold (all of Windows 10) return false; 22000 and up return true.
constexpr bool isWin11OrLater(unsigned long buildNumber) { return buildNumber >= 22000UL; }

} // namespace dish::chrome
