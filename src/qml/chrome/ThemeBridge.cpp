// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/ThemeBridge.h"

#include "UI/Theme.h"

namespace dish::chrome {

ThemeBridge::ThemeBridge(QObject* parent) : QObject(parent) {}

QColor ThemeBridge::background() const { return QColor::fromRgba(dish::ui::Theme::background); }
QColor ThemeBridge::surface() const { return QColor::fromRgba(dish::ui::Theme::surface); }
QColor ThemeBridge::surfaceDim() const { return QColor::fromRgba(dish::ui::Theme::surfaceDim); }
QColor ThemeBridge::primary() const { return QColor::fromRgba(dish::ui::Theme::primary); }
QColor ThemeBridge::onSurface() const { return QColor::fromRgba(dish::ui::Theme::onSurface); }
QColor ThemeBridge::muted() const { return QColor::fromRgba(dish::ui::Theme::muted); }
QColor ThemeBridge::outline() const { return QColor::fromRgba(dish::ui::Theme::outline); }
QColor ThemeBridge::success() const { return QColor::fromRgba(dish::ui::Theme::success); }
QColor ThemeBridge::error() const { return QColor::fromRgba(dish::ui::Theme::error); }
QColor ThemeBridge::warning() const { return QColor::fromRgba(dish::ui::Theme::warning); }

} // namespace dish::chrome
