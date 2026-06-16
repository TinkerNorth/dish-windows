// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/QmlEntryPoint.h"

#include "qml/chrome/ChromeBridge.h"
#include "qml/chrome/FramelessWindowChrome.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWindow>

namespace dish::qml {

int runQmlApp(dish::AppModel& /*model*/) {
    // Basic + a custom theme built from the C++ Theme tokens (ThemeBridge). The
    // FluentWinUI3 style needs Qt 6.8 and isn't available here, so we do NOT use
    // it; the chrome (Mica + snap) comes from the native filter, not the style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QQmlApplicationEngine engine;

    // Resolve the singleton instance so we can wire it to the native chrome
    // before QML reads it. The engine owns it (QML_SINGLETON); we keep a borrow.
    auto* bridge = engine.singletonInstance<dish::chrome::ChromeBridge*>(
        QStringLiteral("Dish.Chrome"), QStringLiteral("ChromeBridge"));

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [bridge](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) {
                return;
            }
            // The chrome filter outlives the window (parented to the app). Mica
            // + the native hit-test attach once the platform window exists.
            auto* chrome = new dish::chrome::FramelessWindowChrome(window, qApp);
            qApp->installNativeEventFilter(chrome);
            if (bridge) {
                bridge->setChrome(chrome);
            }
            const bool mica = chrome->applyMicaBackdrop();
            if (bridge) {
                bridge->setMicaActive(mica);
            }
        },
        Qt::DirectConnection);

    engine.loadFromModule(QStringLiteral("Dish.Chrome"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return QGuiApplication::exec();
}

} // namespace dish::qml
