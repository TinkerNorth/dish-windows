// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Exposes the existing C++ design-token palette (dish::ui::Theme) to QML as a
// `Theme` singleton, so the QML chrome reads the SAME hex values the Widgets app
// renders — one source of truth across both UIs. Read-only snapshot of the
// active palette at construction (the migration step doesn't yet re-theme QML
// live; that comes with the runtime theme controller wiring later).
//
// QML/Quick-only; DISH_QML build exclusively.

#pragma once

#include <QColor>
#include <QObject>
#include <QtQml/qqmlregistration.h>

namespace dish::chrome {

class ThemeBridge : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Theme)
    QML_SINGLETON
    // NOTIFY paletteChanged (was CONSTANT): the active C++ palette is now swapped
    // live by setThemeMode, so these tokens re-read when the appearance flips.
    // refresh() (called from the QmlEntryPoint after a theme change) re-emits.
    Q_PROPERTY(QColor background READ background NOTIFY paletteChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceDim READ surfaceDim NOTIFY paletteChanged)
    Q_PROPERTY(QColor primary READ primary NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryDark READ primaryDark NOTIFY paletteChanged)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY paletteChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY paletteChanged)
    Q_PROPERTY(QColor outline READ outline NOTIFY paletteChanged)
    Q_PROPERTY(QColor success READ success NOTIFY paletteChanged)
    Q_PROPERTY(QColor error READ error NOTIFY paletteChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY paletteChanged)
    // Brand-glyph tint (BrandGlyph re-tints by palette, never by state), the
    // disabled-CONTROL foreground, and the drawn-but-unavailable INFORMATION
    // colour. The last two are different mechanisms on purpose: a dead control
    // is faded, an unavailable capability is not.
    Q_PROPERTY(QColor glyph READ glyph NOTIFY paletteChanged)
    Q_PROPERTY(QColor disabledFg READ disabledFg NOTIFY paletteChanged)
    Q_PROPERTY(QColor mutedStrong READ mutedStrong NOTIFY paletteChanged)
    // Inline-alpha tints derived from the ACTIVE accent at read time (the token
    // sheet carries them as rgba literals; deriving keeps them in lockstep with
    // a palette swap for free). Alphas differ per appearance — a light accent
    // needs a quieter wash (design tokens/colors.css).
    Q_PROPERTY(QColor primaryHover READ primaryHover NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryPress READ primaryPress NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryFill READ primaryFill NOTIFY paletteChanged)
    Q_PROPERTY(QColor warningFill READ warningFill NOTIFY paletteChanged)
    // The third accent wash (primaryHover is the 12 % step, primaryPress the
    // 18 %; this is the 24 % one a pressed placeholder/option card wants).
    Q_PROPERTY(QColor accentWash24 READ accentWash24 NOTIFY paletteChanged)
    // Status fills for CapabilityChip's Ok / Warn / Absent tones, mirroring
    // warningFill's derivation from its own base hue.
    Q_PROPERTY(QColor successFill READ successFill NOTIFY paletteChanged)
    Q_PROPERTY(QColor errorFill READ errorFill NOTIFY paletteChanged)
    // The dialog scrim (background @ 60 %) — replaces the hard-coded rgba the
    // dialogs each carried, so a palette swap re-tints the scrim too.
    Q_PROPERTY(QColor scrim READ scrim NOTIFY paletteChanged)
    // The global keyboard-focus ring (accent @ 30 %), drawn OUTSIDE the 1 px
    // primary border on visualFocus.
    Q_PROPERTY(QColor focusRing READ focusRing NOTIFY paletteChanged)
    // A quieter hairline than `outline` (accent @ 9 %) for intra-card dividers.
    Q_PROPERTY(QColor outlineSubtle READ outlineSubtle NOTIFY paletteChanged)
    // The donation accent (pulse pink) + its derived washes: the 12% fill and
    // the 35% edge the Support Dish surface uses (design overlays PULSE_FILL /
    // PULSE_EDGE). One hue beyond cyan, reserved for donations.
    Q_PROPERTY(QColor pulse READ pulse NOTIFY paletteChanged)
    Q_PROPERTY(QColor pulseFill READ pulseFill NOTIFY paletteChanged)
    Q_PROPERTY(QColor pulseEdge READ pulseEdge NOTIFY paletteChanged)

  public:
    explicit ThemeBridge(QObject* parent = nullptr);

    QColor background() const;
    QColor surface() const;
    QColor surfaceDim() const;
    QColor primary() const;
    QColor primaryDark() const;
    QColor onPrimary() const;
    QColor onSurface() const;
    QColor muted() const;
    QColor outline() const;
    QColor success() const;
    QColor error() const;
    QColor warning() const;
    QColor glyph() const;
    QColor disabledFg() const;
    QColor mutedStrong() const;
    QColor primaryHover() const;
    QColor primaryPress() const;
    QColor primaryFill() const;
    QColor warningFill() const;
    QColor accentWash24() const;
    QColor successFill() const;
    QColor errorFill() const;
    QColor scrim() const;
    QColor focusRing() const;
    QColor outlineSubtle() const;
    QColor pulse() const;
    QColor pulseFill() const;
    QColor pulseEdge() const;

    // Re-read the active C++ Theme tokens (call after setActiveAppearance swapped
    // the palette). Every token property re-evaluates off the one paletteChanged.
    Q_INVOKABLE void refresh();

    // Re-alpha any token at an arbitrary opacity (0..1) from QML, so a one-off
    // wash never has to be spelled as a raw Qt.rgba literal in a page.
    Q_INVOKABLE QColor alpha(const QColor& c, qreal a) const;

  signals:
    void paletteChanged();
};

} // namespace dish::chrome
