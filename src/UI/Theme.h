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

#include <QApplication>
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
    // existing call sites (`hex(Theme::primary)`, `dotQss(Theme::success)`, …)
    // keep working verbatim while resolving to the currently-applied appearance.
    // setActivePalette() rewrites them; applyDishTheme()/the QSS helpers read
    // them, so re-applying re-themes the whole app. Initialised to the dark
    // values so a build that never calls setActivePalette() looks exactly as it
    // did before this workstream (no dark regression).
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

// Swap the active palette (the Theme::* tokens above). Pure state mutation — it
// does NOT touch any QApplication; call applyDishTheme(app) afterwards to push
// the new tokens into the global palette + stylesheet. The live-re-apply
// Controller (composer::ThemeController) calls both in order.
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

// Apply the global Qt palette + a stylesheet matching dish-android's themes,
// reading from the *active* palette (so a re-apply after setActivePalette
// re-themes globally-styled widgets).
void applyDishTheme(QApplication& app);

// Format a QRgb as a `#RRGGBB` string for embedding in QSS.
QString hex(QRgb c);

// Style helpers used by the dialogs / SlotCard.
QString sectionHeaderQss();
QString outlinedButtonQss();
QString dotQss(QRgb color);

// Apply the canonical Dish design-system "disabled" treatment to a widget:
// when the widget is disabled, the entire control drops to 0.4 alpha (matches
// `ds-components.jsx`'s Button rule `opacity: disabled ? 0.4 : 1`). Qt
// stylesheets don't accept `opacity:` directly, so the helper installs a
// `QGraphicsOpacityEffect` that toggles on `QEvent::EnabledChange` via a
// child-object filter. Press / hover feedback is naturally suppressed
// because the existing `:hover` / `:pressed` QSS selectors do not match a
// disabled widget. Mirrors dish-mac's `DishOutlinedButtonStyle` opacity rule.
//
// Call this once, after the widget is fully constructed, on any control that
// can transition between enabled / disabled in-flight (Scan, Pair, Connect).
void applyDisabledOpacityEffect(QWidget* widget);

// Small capability-chip pill used in SlotCard. `present` renders the active
// (filled, primary-tinted) chip; otherwise a dimmed, outlined "not available"
// chip. Mirrors dish-mac's CapabilityChip. Colours come from Theme tokens.
QString capabilityChipQss(bool present);

// Battery-chip pill used in SlotCard, sat next to the motion chip. Shares the
// capability-chip pill geometry. `lowBattery` swaps the cyan/primary tint for
// the amber `warning` token so a near-flat pad reads at a glance.
QString batteryChipQss(bool lowBattery);

// Small, unobtrusive live-stats text used in SlotCard for the measured-Hz
// readouts (gamepad / motion / USB-direct poll rate). Deliberately quieter than
// the filled capability pills — a borderless monospace number — so the live
// numbers read as telemetry, not as another status chip. Mirrors android's
// live-stats pills tone split: `measured` (a USB-direct pad's continuously-
// measured rate) renders in the `success` token, an estimated/peak routed rate
// in the muted token.
QString liveStatChipQss(bool measured);

} // namespace dish::ui
