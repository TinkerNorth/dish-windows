// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Theme.h"

#include <QGuiApplication>
#include <QStyleHints>

#ifdef Q_OS_WIN
#include <QSettings>
#include <QVariant>
#endif

namespace dish::ui {

// ── Palettes ────────────────────────────────────────────────────────────────
// The dark palette is the historical deep-space default (lifted from
// dish-android values-night/colors.xml). The light palette mirrors every dark
// token role with a light-appropriate value (the non-night values/colors.xml +
// the cross-client design system in BRAND.md). Every role in the dark set has a
// counterpart here — palette completeness is asserted in test_theme_store.cpp.

const ThemePalette& darkPalette() {
    static const ThemePalette kDark{
        /*background*/ 0xFF060818,
        /*surface*/ 0xFF0C1027,
        /*surfaceDim*/ 0xFF131A3A,
        /*primary*/ 0xFF4FE3FF,
        /*primaryDark*/ 0xFF2C93AD,
        /*onPrimary*/ 0xFF060818,
        /*onSurface*/ 0xFFE6ECFF,
        /*muted*/ 0xFF93A0C8,
        // outline is web rgba(79,227,255,0.18) expressed as ARGB (alpha 0x2E).
        /*outline*/ 0x2E4FE3FF,
        /*success*/ 0xFF22C55E,
        /*error*/ 0xFFE74C3C,
        /*warning*/ 0xFFF59E0B,
        // Donation accent — dish-android colorPulse, pulse pink on navy.
        /*pulse*/ 0xFFFF6FB5,
        // Brand-glyph tint — the light-cyan the SVGs bake, now a token so the
        // light palette can darken it (11.0:1 here, 1.7:1 on white).
        /*glyph*/ 0xFF8FCFE3,
        /*disabledFg*/ 0xFF6C7799,  // 4.2:1 on surface
        /*mutedStrong*/ 0xFF7E8CB4, // 5.6:1 on surface
    };
    return kDark;
}

const ThemePalette& lightPalette() {
    // Light appearance — the non-night design tokens. Body/surface roles invert
    // to near-white; the cyan accent darkens to `primaryDark` so it keeps
    // contrast on a light background (a bright cyan on white is illegible), with
    // a deep-ink `onPrimary`/`onSurface` for text. Status hues stay in family
    // but shift to AA-contrast-on-light variants. Mirrors dish-android's
    // values/colors.xml (light) against values-night/ (dark).
    static const ThemePalette kLight{
        /*background*/ 0xFFF5F7FC,  // body — soft off-white (--tn-ink light)
        /*surface*/ 0xFFFFFFFF,     // card — white (--tn-night light)
        /*surfaceDim*/ 0xFFE7ECF6,  // recessed — light grey (--tn-deep light)
        /*primary*/ 0xFF0E7C97,     // accent — darkened cyan for contrast on white
        /*primaryDark*/ 0xFF0A5E73, // pressed / disabled accent
        /*onPrimary*/ 0xFFFFFFFF,   // text on primary
        /*onSurface*/ 0xFF0C1430,   // body text — deep ink
        /*muted*/ 0xFF5A6680,       // secondary text — slate
        /*outline*/ 0x330E7C97,     // borders — darkened-cyan @ ~20% alpha
        /*success*/ 0xFF1B873F,     // status — success (darker green on light)
        /*error*/ 0xFFC0392B,       // status — error (darker red on light)
        /*warning*/ 0xFFB7791F,     // status — warning (amber that reads on white)
        /*pulse*/ 0xFFC2417F,       // donation accent — pink, AA-darkened on white
        /*glyph*/ 0xFF2F7E96,       // brand glyph — darkened cyan (4.6:1 on white)
        // NOTE: disabledFg is deliberately LIGHTER than its dark counterpart —
        // "disabled" reads as receding, which on a white card means paler.
        /*disabledFg*/ 0xFF8A93A6,  // 3.1:1 on surface
        /*mutedStrong*/ 0xFF4B566E, // 7.4:1 on surface
    };
    return kLight;
}

const ThemePalette& paletteFor(Appearance appearance) {
    return appearance == Appearance::Light ? lightPalette() : darkPalette();
}

// ── Active palette (the Theme::* tokens) ────────────────────────────────────
// Initialised to the dark values so a build that never calls setActivePalette()
// is pixel-identical to the pre-3d app.

QRgb Theme::background = darkPalette().background;
QRgb Theme::surface = darkPalette().surface;
QRgb Theme::surfaceDim = darkPalette().surfaceDim;
QRgb Theme::primary = darkPalette().primary;
QRgb Theme::primaryDark = darkPalette().primaryDark;
QRgb Theme::onPrimary = darkPalette().onPrimary;
QRgb Theme::onSurface = darkPalette().onSurface;
QRgb Theme::muted = darkPalette().muted;
QRgb Theme::outline = darkPalette().outline;
QRgb Theme::success = darkPalette().success;
QRgb Theme::error = darkPalette().error;
QRgb Theme::warning = darkPalette().warning;
QRgb Theme::pulse = darkPalette().pulse;
QRgb Theme::glyph = darkPalette().glyph;
QRgb Theme::disabledFg = darkPalette().disabledFg;
QRgb Theme::mutedStrong = darkPalette().mutedStrong;

namespace {
Appearance g_activeAppearance = Appearance::Dark;
} // namespace

void setActivePalette(const ThemePalette& palette) {
    Theme::background = palette.background;
    Theme::surface = palette.surface;
    Theme::surfaceDim = palette.surfaceDim;
    Theme::primary = palette.primary;
    Theme::primaryDark = palette.primaryDark;
    Theme::onPrimary = palette.onPrimary;
    Theme::onSurface = palette.onSurface;
    Theme::muted = palette.muted;
    Theme::outline = palette.outline;
    Theme::success = palette.success;
    Theme::error = palette.error;
    Theme::warning = palette.warning;
    Theme::pulse = palette.pulse;
    Theme::glyph = palette.glyph;
    Theme::disabledFg = palette.disabledFg;
    Theme::mutedStrong = palette.mutedStrong;
}

Appearance activeAppearance() { return g_activeAppearance; }

void setActiveAppearance(Appearance appearance) {
    g_activeAppearance = appearance;
    setActivePalette(paletteFor(appearance));
}

Appearance detectSystemAppearance() {
#ifdef Q_OS_WIN
    // Windows personalisation: AppsUseLightTheme is 1 for light, 0 for dark.
    QSettings personalize(
        QStringLiteral(
            "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize"),
        QSettings::NativeFormat);
    const QVariant appsLight = personalize.value(QStringLiteral("AppsUseLightTheme"));
    if (appsLight.isValid()) {
        return appsLight.toInt() != 0 ? Appearance::Light : Appearance::Dark;
    }
#endif
    // Qt 6 style hint where the registry value is missing (or on non-Windows).
    if (auto* hints = QGuiApplication::styleHints()) {
        if (hints->colorScheme() == Qt::ColorScheme::Light) { return Appearance::Light; }
        if (hints->colorScheme() == Qt::ColorScheme::Dark) { return Appearance::Dark; }
    }
    return Appearance::Dark;
}

QString hex(QRgb c) {
    return QStringLiteral("#%1%2%3")
        .arg(qRed(c), 2, 16, QLatin1Char('0'))
        .arg(qGreen(c), 2, 16, QLatin1Char('0'))
        .arg(qBlue(c), 2, 16, QLatin1Char('0'));
}

} // namespace dish::ui
