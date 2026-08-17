// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/chrome/ChromeSingletons.h"

#include "qml/chrome/ChromeBridge.h"
#include "qml/chrome/ThemeBridge.h"
#include "qml/chrome/TokensBridge.h"

#include "UI/Theme.h"

#include <QGuiApplication>
#include <QQmlEngine>
#include <qqml.h>

namespace dish::chrome {

ChromeSingletons registerChromeSingletons() {
    // The appearance resolve QmlEntryPoint runs at lines 48-55, minus the theme
    // store: the installer carries no persisted preference, so the OS answer is
    // the whole answer. Resolved before the bridges are read so the first
    // `Theme.*` evaluation already sees the right palette.
    dish::ui::setActiveAppearance(dish::ui::detectSystemAppearance());

    // Registered BY INSTANCE, not via QML_SINGLETON: under LTCG (/GL) the
    // generated QQmlModuleRegistration static initializer is stripped, so the
    // auto-registered names never reach the engine and every `Theme.*` /
    // `ChromeBridge.*` reference becomes a ReferenceError — leaving the window
    // at QtQuick's default white. Declared before the engine so they outlive
    // it; CppOwnership so QML never deletes them.
    auto* bridge = new dish::chrome::ChromeBridge(qApp);
    auto* themeBridge = new dish::chrome::ThemeBridge(qApp);
    auto* tokensBridge = new dish::chrome::TokensBridge(qApp);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(themeBridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(tokensBridge, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "ChromeBridge", bridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Theme", themeBridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Tokens", tokensBridge);

    return ChromeSingletons{bridge, themeBridge, tokensBridge};
}

} // namespace dish::chrome
