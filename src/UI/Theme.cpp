// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Theme.h"

#include <QEvent>
#include <QGraphicsOpacityEffect>
#include <QGuiApplication>
#include <QObject>
#include <QPalette>
#include <QStyleHints>
#include <QWidget>

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

namespace {

// A `rgba(r,g,b,a)` QSS fragment derived from a token at the given alpha. Used
// for the hover / pressed / chip-fill tints that need inline alpha (QSS has no
// variable references and no half-alpha token). Deriving them from the active
// palette means a light re-theme tints them with the light accent, not the
// hardcoded dark cyan.
QString rgba(QRgb c, double alpha) {
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(qRed(c))
        .arg(qGreen(c))
        .arg(qBlue(c))
        .arg(alpha, 0, 'f', 2);
}

} // namespace

void applyDishTheme(QApplication& app) {
    QPalette p;
    const QColor bg(Theme::background);
    const QColor surface(Theme::surface);
    const QColor onSurface(Theme::onSurface);
    const QColor primary(Theme::primary);
    p.setColor(QPalette::Window, bg);
    p.setColor(QPalette::WindowText, onSurface);
    p.setColor(QPalette::Base, surface);
    p.setColor(QPalette::AlternateBase, QColor(Theme::surfaceDim));
    p.setColor(QPalette::ToolTipBase, surface);
    p.setColor(QPalette::ToolTipText, onSurface);
    p.setColor(QPalette::Text, onSurface);
    p.setColor(QPalette::Button, surface);
    p.setColor(QPalette::ButtonText, onSurface);
    p.setColor(QPalette::Highlight, primary);
    p.setColor(QPalette::HighlightedText, QColor(Theme::onPrimary));
    p.setColor(QPalette::Disabled, QPalette::Text, QColor(Theme::muted));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(Theme::muted));
    app.setPalette(p);

    const QString qss =
        QStringLiteral(
            "QMainWindow, QDialog { background-color: %1; }"
            "QWidget { color: %2; font-family: 'Inter','Roboto',sans-serif; font-size: 13px; }"
            "QFrame#card { background-color: %3; border: 1px solid %4; border-radius: 8px; }"
            "QLabel#section { font-family: monospace; color: %5; letter-spacing: 1.5px; "
            "                font-size: 11px; }"
            "QPushButton { background: transparent; color: %5; border: 1px solid %5; "
            "             border-radius: 6px; padding: 6px 12px; font-weight: 500; }"
            "QPushButton:hover { background-color: %10; }"
            "QPushButton:pressed { background-color: %11; }"
            "QPushButton:disabled { color: %6; border-color: %6; }"
            "QPushButton#primary { background-color: %5; color: %7; border: none; }"
            "QPushButton#primary:hover { background-color: %8; }"
            "QPushButton#primary:disabled { background-color: %9; color: %6; border: none; }"
            "QListWidget, QTreeWidget { background-color: %3; border: 1px solid %4; "
            "                          border-radius: 8px; padding: 4px; }"
            "QStatusBar { background-color: %3; color: %6; }"
            "QLineEdit { background-color: %3; color: %2; border: 1px solid %4; "
            "           border-radius: 6px; padding: 6px 8px; }"
            "QLineEdit:focus { border-color: %5; }"
            "QProgressBar { background-color: %3; border: 1px solid %4; border-radius: 2px; }"
            "QProgressBar::chunk { background-color: %5; border-radius: 2px; }")
            .arg(hex(Theme::background), hex(Theme::onSurface), hex(Theme::surface),
                 hex(Theme::outline), hex(Theme::primary), hex(Theme::muted), hex(Theme::onPrimary),
                 hex(Theme::primaryDark), hex(Theme::surfaceDim))
            .arg(rgba(Theme::primary, 0.12), rgba(Theme::primary, 0.18));
    app.setStyleSheet(qss);
}

QString sectionHeaderQss() {
    return QStringLiteral(
               "font-family: monospace; color: %1; letter-spacing: 1.5px; font-size: 11px;")
        .arg(hex(Theme::primary));
}

QString outlinedButtonQss() {
    return QStringLiteral(
               "background: transparent; color: %1; border: 1px solid %1; border-radius: 6px; "
               "padding: 6px 12px;")
        .arg(hex(Theme::primary));
}

QString dotQss(QRgb color) {
    return QStringLiteral("background-color: %1; border-radius: 4px;").arg(hex(color));
}

QString capabilityChipQss(bool present) {
    // Mirrors dish-mac's CapabilityChip: a filled primary-tinted pill when the
    // capability is present, a dimmed outlined pill when it is not. The cyan
    // fill is `Theme::primary` at ~14 % alpha — derived from the active palette
    // so a light re-theme uses the light accent. The "off" pill's text and
    // border both reuse `Theme::muted` so the chip reads as a single dimmed unit.
    if (present) {
        return QStringLiteral("color: %1; background-color: %2; "
                              "border: 1px solid transparent; border-radius: 5px; "
                              "padding: 2px 7px; font-size: 10px; font-weight: 500;")
            .arg(hex(Theme::primary), rgba(Theme::primary, 0.14));
    }
    return QStringLiteral("color: %1; background-color: transparent; "
                          "border: 1px solid %1; border-radius: 5px; "
                          "padding: 2px 7px; font-size: 10px; font-weight: 500;")
        .arg(hex(Theme::muted));
}

namespace {

// Event filter that flips a QGraphicsOpacityEffect between 1.0 and 0.4
// whenever the watched widget's enabled state changes. Living as a child of
// the watched widget guarantees lifetime parity: deleting the widget deletes
// the filter, which removes the only reference to the effect. Mirrors
// dish-mac's DishOutlinedButtonStyle which animates opacity 1.0 <-> 0.4
// on isEnabled — same canonical "control is not tappable right now" cue
// at the same opacity value.
class DisabledOpacityFilter : public QObject {
  public:
    DisabledOpacityFilter(QWidget* target, QGraphicsOpacityEffect* effect)
        : QObject(target), effect_(effect) {
        // Apply the initial state so a widget that was constructed disabled
        // is immediately dimmed without waiting for a state-change event.
        effect_->setOpacity(target->isEnabled() ? 1.0 : 0.4);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::EnabledChange) {
            auto* w = qobject_cast<QWidget*>(watched);
            if (w != nullptr) { effect_->setOpacity(w->isEnabled() ? 1.0 : 0.4); }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    QGraphicsOpacityEffect* effect_;
};

} // namespace

void applyDisabledOpacityEffect(QWidget* widget) {
    if (widget == nullptr) { return; }
    // QGraphicsOpacityEffect parented to the widget — Qt takes ownership and
    // disposes of it when the widget is destroyed. Reusable across paint
    // styles (border / hover / pressed) because it composites the whole
    // control as one rendered image at the requested alpha.
    auto* effect = new QGraphicsOpacityEffect(widget);
    effect->setOpacity(widget->isEnabled() ? 1.0 : 0.4);
    widget->setGraphicsEffect(effect);
    widget->installEventFilter(new DisabledOpacityFilter(widget, effect));
}

QString batteryChipQss(bool lowBattery) {
    // Same pill geometry as capabilityChipQss's "present" branch. A healthy
    // battery reuses the cyan `primary` tint; a low battery (< ~15 %) swaps to
    // the amber `warning` token so the player can't miss it. The faint fill
    // alpha is derived from the active palette so a light re-theme retints.
    if (lowBattery) {
        return QStringLiteral("color: %1; background-color: %2; "
                              "border: 1px solid transparent; border-radius: 5px; "
                              "padding: 2px 7px; font-size: 10px; font-weight: 600;")
            .arg(hex(Theme::warning), rgba(Theme::warning, 0.16));
    }
    return QStringLiteral("color: %1; background-color: %2; "
                          "border: 1px solid transparent; border-radius: 5px; "
                          "padding: 2px 7px; font-size: 10px; font-weight: 500;")
        .arg(hex(Theme::primary), rgba(Theme::primary, 0.14));
}

} // namespace dish::ui
