// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Exhaustive tests for the PURE frameless-window hit-test (src/qml/chrome/
// WindowHitTest.h): caption vs maximize-button vs client vs all 8 resize
// regions, the off-by-one at button/rect edges (half-open high edges), the
// maximized suppression of resize borders, and the Win11 build-number gate.
// No Qt, no window — the math is plain ints.

#include "qml/chrome/WindowHitTest.h"

#include <catch2/catch_test_macros.hpp>

using dish::chrome::HitRegion;
using dish::chrome::HitTestInput;
using dish::chrome::hitTest;
using dish::chrome::isWin11OrLater;
using dish::chrome::Point;
using dish::chrome::Rect;

namespace {

// A 800x600 window; a 40px-tall caption strip across the full width; a 46px
// maximize button at x in [600,646) inside the caption; an 8px resize border.
HitTestInput make(int x, int y, bool maximized = false) {
    HitTestInput in{};
    in.cursor = Point{x, y};
    in.window = Rect{0, 0, 800, 600};
    in.caption = Rect{0, 0, 800, 40};
    in.maximizeButton = Rect{600, 0, 646, 40};
    in.resizeBorder = 8;
    in.maximized = maximized;
    return in;
}

} // namespace

TEST_CASE("center of window is client", "[hittest]") {
    CHECK(hitTest(make(400, 300)) == HitRegion::Client);
}

TEST_CASE("caption strip away from buttons is draggable caption", "[hittest]") {
    // y=20 is below the 8px top-resize band, x=200 is left of the max button.
    CHECK(hitTest(make(200, 20)) == HitRegion::Caption);
}

TEST_CASE("maximize button wins over caption (needed for snap)", "[hittest]") {
    CHECK(hitTest(make(620, 20)) == HitRegion::MaximizeButton);
}

TEST_CASE("all four resize edges", "[hittest]") {
    CHECK(hitTest(make(0, 300)) == HitRegion::Left);
    CHECK(hitTest(make(799, 300)) == HitRegion::Right);
    // Top edge mid-width: the top band wins even though it overlaps the caption.
    CHECK(hitTest(make(400, 0)) == HitRegion::Top);
    CHECK(hitTest(make(400, 599)) == HitRegion::Bottom);
}

TEST_CASE("all four resize corners", "[hittest]") {
    CHECK(hitTest(make(0, 0)) == HitRegion::TopLeft);
    CHECK(hitTest(make(799, 0)) == HitRegion::TopRight);
    CHECK(hitTest(make(0, 599)) == HitRegion::BottomLeft);
    CHECK(hitTest(make(799, 599)) == HitRegion::BottomRight);
}

TEST_CASE("resize border thickness boundary (8px band)", "[hittest]") {
    // x=7 is inside the left band (< 0+8); x=8 is just outside it.
    CHECK(hitTest(make(7, 300)) == HitRegion::Left);
    CHECK(hitTest(make(8, 300)) == HitRegion::Client);
    // Right band: x>=800-8=792 is Right; x=791 is not.
    CHECK(hitTest(make(792, 300)) == HitRegion::Right);
    CHECK(hitTest(make(791, 300)) == HitRegion::Client);
}

TEST_CASE("top band still overlaps caption, then caption below it", "[hittest]") {
    // y=7 within top band at a caption x -> Top (resize wins).
    CHECK(hitTest(make(200, 7)) == HitRegion::Top);
    // y=8 below the band but within the 40px caption -> Caption.
    CHECK(hitTest(make(200, 8)) == HitRegion::Caption);
}

TEST_CASE("maximize button half-open edges (off-by-one)", "[hittest]") {
    // Left edge x=600 is inside; x=599 is caption.
    CHECK(hitTest(make(600, 20)) == HitRegion::MaximizeButton);
    CHECK(hitTest(make(599, 20)) == HitRegion::Caption);
    // Right edge x=645 is the last inside pixel; x=646 falls back to caption.
    CHECK(hitTest(make(645, 20)) == HitRegion::MaximizeButton);
    CHECK(hitTest(make(646, 20)) == HitRegion::Caption);
    // Bottom edge of the button at y=39 inside; y=40 is below the caption too.
    CHECK(hitTest(make(620, 39)) == HitRegion::MaximizeButton);
    CHECK(hitTest(make(620, 40)) == HitRegion::Client);
}

TEST_CASE("caption strip half-open bottom edge", "[hittest]") {
    // y=39 last caption row; y=40 is client (below the 40px strip).
    CHECK(hitTest(make(200, 39)) == HitRegion::Caption);
    CHECK(hitTest(make(200, 40)) == HitRegion::Client);
}

TEST_CASE("cursor outside the window is client (defensive)", "[hittest]") {
    CHECK(hitTest(make(-1, 300)) == HitRegion::Client);
    CHECK(hitTest(make(800, 300)) == HitRegion::Client);
    CHECK(hitTest(make(400, 600)) == HitRegion::Client);
}

TEST_CASE("maximized window suppresses all resize regions", "[hittest]") {
    // Corners/edges that would resize a normal window must NOT when maximized.
    CHECK(hitTest(make(0, 0, /*maximized=*/true)) == HitRegion::Caption);
    CHECK(hitTest(make(799, 0, true)) == HitRegion::Caption);
    CHECK(hitTest(make(0, 300, true)) == HitRegion::Client);
    CHECK(hitTest(make(799, 599, true)) == HitRegion::Client);
    // Caption + maximize button still resolve when maximized.
    CHECK(hitTest(make(200, 20, true)) == HitRegion::Caption);
    CHECK(hitTest(make(620, 20, true)) == HitRegion::MaximizeButton);
}

TEST_CASE("zero resize border behaves like no resize frame", "[hittest]") {
    HitTestInput in = make(0, 0);
    in.resizeBorder = 0;
    // Corner with no border -> falls through to caption (it's in the strip).
    CHECK(hitTest(in) == HitRegion::Caption);
}

TEST_CASE("maximize-button rect outside the caption still wins over client",
          "[hittest]") {
    // Defensive: a button placed below the caption strip should still be a
    // maximize hit (the button check is independent of the caption check).
    HitTestInput in = make(620, 100);
    in.maximizeButton = Rect{600, 80, 646, 120};
    in.caption = Rect{0, 0, 800, 40};
    CHECK(hitTest(in) == HitRegion::MaximizeButton);
}

// ── Win11 version gate ──────────────────────────────────────────────────────

TEST_CASE("Win11 gate: 22000 boundary", "[win11gate]") {
    CHECK_FALSE(isWin11OrLater(21999));  // last Windows 10 insider build
    CHECK(isWin11OrLater(22000));        // Windows 11 RTM
    CHECK(isWin11OrLater(22621));        // Win11 22H2
    CHECK(isWin11OrLater(26100));        // Win11 24H2
}

TEST_CASE("Win11 gate: Windows 10 builds are false", "[win11gate]") {
    CHECK_FALSE(isWin11OrLater(0));
    CHECK_FALSE(isWin11OrLater(10240));  // Win10 1507
    CHECK_FALSE(isWin11OrLater(19045));  // Win10 22H2
}
