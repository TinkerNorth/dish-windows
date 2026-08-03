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
#include "qml/chrome/TokensBridge.h"
#include "UI/common/ExternalLink.h"

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
    // FluentWinUI3 needs Qt 6.8 and is unavailable here; the Win11 look comes
    // from the native chrome filter, not the style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Uncreatable: instances are only ever vended through App.slotModel /
    // App.connectionModel, but QML must be able to name the type in a delegate.
    qmlRegisterUncreatableType<dish::qml::SlotListModel>(
        "Dish.Chrome", 1, 0, "SlotListModel",
        QStringLiteral("SlotListModel is owned by AppViewModel"));
    qmlRegisterUncreatableType<dish::qml::ConnectionListModel>(
        "Dish.Chrome", 1, 0, "ConnectionListModel",
        QStringLiteral("ConnectionListModel is owned by AppViewModel"));

    dish::qml::AppViewModel appVm(&model);

    // model.start() already ran the ThemeController, but depending on who ran
    // last let a System+dark cold start paint the body light under a dark title
    // bar. Re-resolving here unconditionally (idempotent) makes the active
    // palette provably match the mode before the Theme singleton is first read.
    {
        const auto mode = model.themeStore()->mode();
        const auto appearance = mode == dish::source::ThemeMode::Light ? dish::ui::Appearance::Light
                                : mode == dish::source::ThemeMode::Dark
                                    ? dish::ui::Appearance::Dark
                                    : dish::ui::detectSystemAppearance();
        dish::ui::setActiveAppearance(appearance);
    }

    // Registered BY INSTANCE, not via QML_SINGLETON: under this target's LTCG
    // (/GL) the generated QQmlModuleRegistration static initializer is stripped,
    // so the auto-registered names never reach the engine and every `Theme.*` /
    // `ChromeBridge.*` reference becomes a ReferenceError — leaving the window
    // at QtQuick's default white. Declared before the engine so they outlive it;
    // CppOwnership so QML never deletes them.
    auto* bridge = new dish::chrome::ChromeBridge(qApp);
    auto* themeBridge = new dish::chrome::ThemeBridge(qApp);
    auto* tokensBridge = new dish::chrome::TokensBridge(qApp);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(themeBridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(tokensBridge, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "ChromeBridge", bridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Theme", themeBridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Tokens", tokensBridge);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &appVm);

    // The theme sink below outlives this scope but the chrome only exists once
    // the window does, so the sink borrows it indirectly. Null until then.
    auto chromeHolder = std::make_shared<dish::chrome::FramelessWindowChrome*>(nullptr);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [bridge, themeBridge, chromeHolder](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) { return; }
            // Parented to the app, not the window: it must survive the messages
            // that still pump while the engine tears the window down.
            auto* chrome = new dish::chrome::FramelessWindowChrome(window, qApp);
            *chromeHolder = chrome;
            qApp->installNativeEventFilter(chrome);
            if (bridge) { bridge->setChrome(chrome); }
            const bool mica = chrome->applyMicaBackdrop();
            // The theme-applied sink only fires on a later user toggle, so the
            // STARTUP appearance has to be pushed here or a cold start in Light
            // keeps the dark Mica backdrop until the user toggles the theme.
            const bool startupDark = dish::ui::activeAppearance() == dish::ui::Appearance::Dark;
            chrome->setImmersiveDarkMode(startupDark);
            if (bridge) {
                bridge->setMicaActive(mica);
                bridge->setDark(startupDark);
            }
            // Any binding that evaluated before the palette settled re-reads
            // now that the window exists.
            if (themeBridge) { themeBridge->refresh(); }
        },
        Qt::DirectConnection);

    // A false return falls through to App.errorMessage (the QML toast channel).
    appVm.setExternalOpenSink([](const QString& url) { return dish::ui::openExternalUrl(url); });

    // The frame must re-theme with the body, or it drifts light while the body
    // re-darks.
    appVm.setThemeAppliedSink([themeBridge, chromeHolder, bridge](bool dark) {
        if (themeBridge) { themeBridge->refresh(); }
        if (bridge) {
            // Drives Main.qml's transparent-vs-solid background: a light app over
            // a dark desktop must not keep the dark Mica backdrop showing through.
            bridge->setDark(dark);
        }
        if (*chromeHolder) { (*chromeHolder)->setImmersiveDarkMode(dark); }
    });

    engine.loadFromModule(QStringLiteral("Dish.Chrome"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) { return 1; }

    return QGuiApplication::exec();
}

} // namespace dish::qml
