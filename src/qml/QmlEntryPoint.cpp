// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/QmlEntryPoint.h"

#include "AppModel.h"
#include "qml/AppViewModel.h"
#include "qml/ConnectionListModel.h"
#include "qml/SlotListModel.h"
#include "qml/chrome/ChromeBridge.h"
#include "qml/chrome/FramelessWindowChrome.h"
#include "qml/chrome/ThemeBridge.h"
#include "ui/common/ExternalLink.h"

#include "UI/Theme.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWindow>
#include <qqml.h>

namespace dish::qml {

int runQmlApp(dish::AppModel& model) {
    // Basic + a custom theme built from the C++ Theme tokens (ThemeBridge). The
    // FluentWinUI3 style needs Qt 6.8 and isn't available here, so we do NOT use
    // it; the chrome (Mica + snap) comes from the native filter, not the style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // The AppViewModel (and the two role models it owns) is the frozen QML
    // contract surface — registered as a context property so every page reads
    // `App.*` without re-importing. The model outlives the engine (both are on
    // this stack frame). The role-bearing list models are registered uncreatable
    // so QML can name their role enums / type in delegates if needed; instances
    // are only ever vended through App.slots / App.connections.
    qmlRegisterUncreatableType<dish::qml::SlotListModel>(
        "Dish.Chrome", 1, 0, "SlotListModel",
        QStringLiteral("SlotListModel is owned by AppViewModel"));
    qmlRegisterUncreatableType<dish::qml::ConnectionListModel>(
        "Dish.Chrome", 1, 0, "ConnectionListModel",
        QStringLiteral("ConnectionListModel is owned by AppViewModel"));

    dish::qml::AppViewModel appVm(&model);

    // model.start() already ran the ThemeController, which resolved the persisted
    // mode (System -> the OS appearance) and swapped the active palette. Re-resolve
    // System here so the active palette provably matches the OS at the instant the
    // QML Theme singleton is registered and first read — an explicit Light/Dark
    // choice is already applied by the controller and left untouched. (A later
    // setThemeMode re-themes live via the AppViewModel theme-applied sink below.)
    if (model.themeStore()->mode() == dish::source::ThemeMode::System) {
        dish::ui::setActiveAppearance(dish::ui::detectSystemAppearance());
    }

    // Own the two QML singletons explicitly and register them by instance, rather
    // than relying on QML_SINGLETON auto-registration. Under this target's LTCG
    // (/GL) the generated QQmlModuleRegistration static initializer is stripped,
    // so the auto-registered `ChromeBridge`/`Theme` names never reach the engine —
    // every `Theme.*` / `ChromeBridge.*` reference in QML resolves to a
    // ReferenceError, leaving the window at QtQuick's default WHITE and the body
    // unthemed. Registering the instances here makes the names resolvable AND lets
    // us hand QML the very objects we wire to the native chrome / re-theme sink.
    // They are declared before the engine so they outlive it. QML must not delete
    // them, so register with CppOwnership.
    auto* bridge = new dish::chrome::ChromeBridge(qApp);
    auto* themeBridge = new dish::chrome::ThemeBridge(qApp);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(themeBridge, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "ChromeBridge", bridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Theme", themeBridge);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &appVm);

    // A shared borrow of the chrome filter so the theme-applied sink (below) can
    // flip the native immersive-dark attribute after the window exists. Filled by
    // the objectCreated handler; null until then.
    auto chromeHolder = std::make_shared<dish::chrome::FramelessWindowChrome*>(nullptr);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [bridge, chromeHolder](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) {
                return;
            }
            // The chrome filter outlives the window (parented to the app). Mica
            // + the native hit-test attach once the platform window exists.
            auto* chrome = new dish::chrome::FramelessWindowChrome(window, qApp);
            *chromeHolder = chrome;
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

    // Route App.openExternalUrl through the shared ExternalLink helper so a
    // failure raises the same warning the Widgets screens do. No NotificationQueue
    // exists on the Quick path yet, so failures fall through to App.errorMessage
    // (the QML toast channel) — pass nullptr and let openExternalUrl return false;
    // the AppViewModel's own emit covers the toast.
    appVm.setExternalOpenSink(
        [](const QString& url) { return dish::ui::openExternalUrl(url, nullptr); });

    // After a theme change: re-read the C++ tokens into the QML Theme singleton
    // and flip the native chrome's immersive-dark attribute so the frame matches.
    appVm.setThemeAppliedSink([themeBridge, chromeHolder](bool dark) {
        if (themeBridge) {
            themeBridge->refresh();
        }
        if (*chromeHolder) {
            (*chromeHolder)->setImmersiveDarkMode(dark);
        }
    });

    engine.loadFromModule(QStringLiteral("Dish.Chrome"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) {
        return 1;
    }

    return QGuiApplication::exec();
}

} // namespace dish::qml
