// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Color palette lifted verbatim from dish-android/res/values/colors.xml so
// every client renders identically side-by-side. Same hex values as
// dish-mac/UI/Theme.swift.
//
// Workstream 3d (Settings + full theme): this file now carries BOTH the dark
// palette (unchanged, the deep-space default) AND a light palette mirroring
// every dark token role, plus a selectable "active palette" so the app can
// re-theme at runtime (dark / light / system). The `Theme::primary` etc.
// accessors keep the exact call-site syntax every other widget uses — they read
// the active palette, so a re-apply re-themes the whole app. See DESIGN.md for
// the token <-> hex mapping (now documenting both palettes) and the cross-repo
// schema (BRAND.md). The light tokens MUST stay in lockstep with dish-android's
// non-night `values/` resources and the other clients.

#pragma once

#include <QColor>
#include <QString>

namespace dish::ui {

// The full set of design-token colours for one appearance (dark or light).
// Mirrors the DESIGN.md token table 1:1 — every role has a value, none is left
// palette-specific. Pure data: no behaviour, no Source. The active instance is
// selected by setActivePalette(); the Theme accessors read from it.
struct ThemePalette {
    QRgb background;
    QRgb surface;
    QRgb surfaceDim;
    QRgb primary;
    QRgb primaryDark;
    QRgb onPrimary;
    QRgb onSurface;
    QRgb muted;
    QRgb outline;
    QRgb success;
    QRgb error;
    QRgb warning;
};

// Which appearance the app is rendering. SYSTEM is resolved to one of
// dark/light by the OS-preference reader before a palette is selected, so the
// active palette is always a concrete dark/light set. Mirrors dish-android's
// ThemeMode { SYSTEM, LIGHT, DARK } (source/store/ThemePreferenceStore.kt) —
// the *resolved* half of it (SYSTEM never reaches the palette selection).
enum class Appearance { Dark, Light };

// The canonical dark palette (the historical deep-space default) and the new
// light palette. Exposed so the completeness test can iterate both and assert
// every role differs / is populated. constexpr — pure compile-time data.
const ThemePalette& darkPalette();
const ThemePalette& lightPalette();

// Resolve an Appearance to its palette.
const ThemePalette& paletteFor(Appearance appearance);

struct Theme {
    // Cyan / deep-space palette (dark) by default — mirrors dish-website. See
    // DESIGN.md for the token name <-> hex mapping and the cross-repo schema.
    //
    // These are NON-const statics that alias the *active* palette's fields, so
    // call sites (the QML ThemeBridge accessors) resolve to the currently-
    // applied appearance. setActivePalette() rewrites them. Initialised to the
    // dark values so a build that never calls setActivePalette() renders the
    // deep-space default.
    static QRgb background;
    static QRgb surface;
    static QRgb surfaceDim;
    static QRgb primary;
    static QRgb primaryDark;
    static QRgb onPrimary;
    static QRgb onSurface;
    static QRgb muted;
    static QRgb outline;
    static QRgb success;
    static QRgb error;
    static QRgb warning;
};

// Swap the active palette (the Theme::* tokens above). Pure state mutation;
// the QML ThemeBridge re-reads these on its refresh() and the native chrome
// flips its immersive-dark attribute — there is no widget stylesheet anymore.
void setActivePalette(const ThemePalette& palette);

// The currently-active appearance (Dark unless setActiveAppearance set Light).
Appearance activeAppearance();
void setActiveAppearance(Appearance appearance);

// OS appearance-preference seam. Reads the Windows
// HKCU\...\Themes\Personalize\AppsUseLightTheme value (0 -> Dark, 1 -> Light),
// falling back to the Qt 6 QStyleHints::colorScheme() where the registry value
// is absent, and to Dark if neither resolves. Injected as a std::function in
// tests so SYSTEM resolution can be driven without touching the real registry.
Appearance detectSystemAppearance();

// Format a QRgb as a `#RRGGBB` string (diagnostics + any string-styled sink).
QString hex(QRgb c);

} // namespace dish::ui
