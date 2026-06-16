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
    Q_PROPERTY(QColor background READ background CONSTANT)
    Q_PROPERTY(QColor surface READ surface CONSTANT)
    Q_PROPERTY(QColor surfaceDim READ surfaceDim CONSTANT)
    Q_PROPERTY(QColor primary READ primary CONSTANT)
    Q_PROPERTY(QColor onSurface READ onSurface CONSTANT)
    Q_PROPERTY(QColor muted READ muted CONSTANT)
    Q_PROPERTY(QColor outline READ outline CONSTANT)
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor error READ error CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)

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
};

} // namespace dish::chrome
