// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/ThemeBridge.h"

#include "UI/Theme.h"

namespace dish::chrome {

namespace {

// The design tokens carry the accent washes as rgba literals per appearance;
// deriving them from the live accent keeps a palette swap atomic (no second
// table to forget). Alpha is 0..255.
QColor withAlpha(QRgb base, int alpha) {
    QColor c = QColor::fromRgba(base);
    c.setAlpha(alpha);
    return c;
}

bool lightActive() { return dish::ui::activeAppearance() == dish::ui::Appearance::Light; }

} // namespace

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent) {}

QColor ThemeBridge::background() const { return QColor::fromRgba(dish::ui::Theme::background); }
QColor ThemeBridge::surface() const { return QColor::fromRgba(dish::ui::Theme::surface); }
QColor ThemeBridge::surfaceDim() const { return QColor::fromRgba(dish::ui::Theme::surfaceDim); }
QColor ThemeBridge::primary() const { return QColor::fromRgba(dish::ui::Theme::primary); }
QColor ThemeBridge::primaryDark() const { return QColor::fromRgba(dish::ui::Theme::primaryDark); }
QColor ThemeBridge::onPrimary() const { return QColor::fromRgba(dish::ui::Theme::onPrimary); }
QColor ThemeBridge::onSurface() const { return QColor::fromRgba(dish::ui::Theme::onSurface); }
QColor ThemeBridge::muted() const { return QColor::fromRgba(dish::ui::Theme::muted); }
QColor ThemeBridge::outline() const { return QColor::fromRgba(dish::ui::Theme::outline); }
QColor ThemeBridge::success() const { return QColor::fromRgba(dish::ui::Theme::success); }
QColor ThemeBridge::error() const { return QColor::fromRgba(dish::ui::Theme::error); }
QColor ThemeBridge::warning() const { return QColor::fromRgba(dish::ui::Theme::warning); }

// tokens/colors.css: dark hover/press/fill = 12/18/14 %, light = 10/16/12 %;
// warning fill is 16 % in both.
QColor ThemeBridge::primaryHover() const {
    return withAlpha(dish::ui::Theme::primary, lightActive() ? 26 : 31);
}
QColor ThemeBridge::primaryPress() const {
    return withAlpha(dish::ui::Theme::primary, lightActive() ? 41 : 46);
}
QColor ThemeBridge::primaryFill() const {
    return withAlpha(dish::ui::Theme::primary, lightActive() ? 31 : 36);
}
QColor ThemeBridge::warningFill() const { return withAlpha(dish::ui::Theme::warning, 41); }

// Donation accent (design overlays: PULSE #FF6FB5, PULSE_FILL 12 %, PULSE_EDGE
// 35 % — the alphas are appearance-invariant; the base hue AA-darkens on light).
QColor ThemeBridge::pulse() const { return QColor::fromRgba(dish::ui::Theme::pulse); }
QColor ThemeBridge::pulseFill() const { return withAlpha(dish::ui::Theme::pulse, 31); }
QColor ThemeBridge::pulseEdge() const { return withAlpha(dish::ui::Theme::pulse, 89); }

void ThemeBridge::refresh() { emit paletteChanged(); }

} // namespace dish::chrome
