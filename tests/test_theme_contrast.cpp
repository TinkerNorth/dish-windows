// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The light-palette contrast guard. The dark palette was designed against a
// near-black body and comfortably clears every threshold; the light palette was
// derived from it and is where a token quietly stops being readable — the
// computed audit found the brand glyph at 1.7:1 on a white card and the accent
// at exactly the AA floor on the light body.
//
// So: compute WCAG 2.1 relative luminance over the ACTUAL ThemePalette values
// and assert the floors, in BOTH palettes. A future token edit that looks fine
// on dark and vanishes on light fails here instead of in a screenshot.
//
// Thresholds (WCAG 2.1):
//   4.5 : 1  text (any size the app actually uses -- nothing here is "large")
//   3.0 : 1  graphical objects, and inactive-control foregrounds
//
// disabledFg sits at the 3:1 bar deliberately: it is a control that is not
// actionable, and WCAG exempts inactive controls entirely -- 3:1 is a floor this
// app chose, not one it inherited. mutedStrong sits at 4.5 because it carries
// INFORMATION (why a capability is unavailable), which is never exempt.

#include "UI/Theme.h"

#include <catch2/catch_test_macros.hpp>

#include <QColor>

#include <cmath>

using dish::ui::darkPalette;
using dish::ui::hex;
using dish::ui::lightPalette;
using dish::ui::ThemePalette;

namespace {

// WCAG 2.1 relative luminance of an sRGB channel.
double channelLuminance(int value8) {
    const double c = static_cast<double>(value8) / 255.0;
    return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double relativeLuminance(QRgb rgb) {
    return 0.2126 * channelLuminance(qRed(rgb)) + 0.7152 * channelLuminance(qGreen(rgb)) +
           0.0722 * channelLuminance(qBlue(rgb));
}

// The WCAG contrast ratio between two OPAQUE colours. Every role asserted here
// is opaque by construction (test_theme_store.cpp pins that separately), so no
// alpha compositing is needed.
double contrastRatio(QRgb a, QRgb b) {
    const double la = relativeLuminance(a);
    const double lb = relativeLuminance(b);
    const double hi = la > lb ? la : lb;
    const double lo = la > lb ? lb : la;
    return (hi + 0.05) / (lo + 0.05);
}

// Assert with the failing hex pair in the message, so a red build names the
// token that moved rather than a bare number.
void requireContrast(QRgb fg, QRgb bg, double floorRatio, const char* what) {
    const double ratio = contrastRatio(fg, bg);
    INFO(what << ": " << hex(fg).toStdString() << " on " << hex(bg).toStdString() << " = " << ratio
              << " : 1, floor " << floorRatio);
    REQUIRE(ratio >= floorRatio);
}

// Every threshold, for one palette. Called for dark and for light so neither can
// be tightened at the other's expense.
void requirePaletteContrast(const ThemePalette& p, const char* palette) {
    INFO("palette: " << palette);
    // Text roles -- 4.5:1.
    requireContrast(p.onSurface, p.surface, 4.5, "onSurface on surface");
    requireContrast(p.muted, p.surface, 4.5, "muted on surface");
    requireContrast(p.mutedStrong, p.surface, 4.5, "mutedStrong on surface");
    // The accent carries every 10 px chip and 11 px section label ON THE BODY,
    // which is the tighter of its two backgrounds in the light palette.
    requireContrast(p.primary, p.background, 4.5, "primary on background");
    // Graphical objects and inactive controls -- 3:1.
    requireContrast(p.disabledFg, p.surface, 3.0, "disabledFg on surface");
    requireContrast(p.glyph, p.surface, 3.0, "glyph on surface");
    requireContrast(p.success, p.surface, 3.0, "success on surface");
    requireContrast(p.warning, p.surface, 3.0, "warning on surface");
    requireContrast(p.error, p.surface, 3.0, "error on surface");
}

} // namespace

TEST_CASE("dark palette clears every contrast floor", "[theme][contrast]") {
    requirePaletteContrast(darkPalette(), "dark");
}

TEST_CASE("light palette clears every contrast floor", "[theme][contrast]") {
    requirePaletteContrast(lightPalette(), "light");
}

TEST_CASE("text on the filled accent is legible in both palettes", "[theme][contrast]") {
    // The rule that makes onPrimary worth having as a separate alias: it is the
    // same hex as the body in dark and a DIFFERENT one in light.
    requireContrast(darkPalette().onPrimary, darkPalette().primary, 4.5, "dark onPrimary");
    requireContrast(lightPalette().onPrimary, lightPalette().primary, 4.5, "light onPrimary");
}

TEST_CASE("the brand glyph is a real token and not the baked SVG hex on light",
          "[theme][contrast]") {
    // The shipped SVGs bake the dark tint. Left untinted it computes to about
    // 1.7:1 on a white card -- below the 3:1 floor for graphical objects, and
    // visually simply gone. This asserts the light palette does NOT reuse it.
    REQUIRE(lightPalette().glyph != darkPalette().glyph);
    REQUIRE(contrastRatio(darkPalette().glyph, lightPalette().surface) < 3.0);
    requireContrast(lightPalette().glyph, lightPalette().surface, 3.0, "light glyph");
}

TEST_CASE("the disabled foreground is never multiplied by an opacity to reach its floor",
          "[theme][contrast]") {
    // The override this token exists for: the old rule dropped an ALREADY muted
    // colour to 0.4 opacity, landing near 2.6:1 dark and 2.0:1 light. The token
    // clears 3:1 on its own, before any opacity is applied.
    for (const auto& p : {darkPalette(), lightPalette()}) {
        requireContrast(p.disabledFg, p.surface, 3.0, "disabledFg unmultiplied");
    }
    // And it is a DIFFERENT colour from mutedStrong: one is a dead control, the
    // other is live information about an unavailable capability.
    REQUIRE(darkPalette().disabledFg != darkPalette().mutedStrong);
    REQUIRE(lightPalette().disabledFg != lightPalette().mutedStrong);
}

TEST_CASE("mutedStrong is readable enough to carry a reason line", "[theme][contrast]") {
    // It renders at FULL opacity by contract -- an unavailable capability is
    // drawn, never faded -- so it must clear the text floor on the card it sits
    // on and on the body behind it.
    for (const auto& p : {darkPalette(), lightPalette()}) {
        requireContrast(p.mutedStrong, p.surface, 4.5, "mutedStrong on surface");
        requireContrast(p.mutedStrong, p.background, 4.5, "mutedStrong on background");
    }
}
