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
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY paletteChanged)
    Q_PROPERTY(QColor muted READ muted NOTIFY paletteChanged)
    Q_PROPERTY(QColor outline READ outline NOTIFY paletteChanged)
    Q_PROPERTY(QColor success READ success NOTIFY paletteChanged)
    Q_PROPERTY(QColor error READ error NOTIFY paletteChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY paletteChanged)

  public:
    explicit ThemeBridge(QObject* parent = nullptr);

    QColor background() const;
    QColor surface() const;
    QColor surfaceDim() const;
    QColor primary() const;
    QColor onSurface() const;
    QColor muted() const;
    QColor outline() const;
    QColor success() const;
    QColor error() const;
    QColor warning() const;

    // Re-read the active C++ Theme tokens (call after setActiveAppearance swapped
    // the palette). Every token property re-evaluates off the one paletteChanged.
    Q_INVOKABLE void refresh();

  signals:
    void paletteChanged();
};

} // namespace dish::chrome
