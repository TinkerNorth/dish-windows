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

    // model.start() already ran the ThemeController, but the smoke checklist
    // caught a System+dark cold start rendering the BODY light while the title
    // bar sampled dark — an ordering interaction between the controller's apply
    // and this entry point. Rather than depend on who ran last, resolve the
    // persisted mode to a concrete appearance HERE, unconditionally, for all
    // three modes (idempotent when the controller already applied the same
    // thing): the active palette then provably matches the mode at the instant
    // the QML Theme singleton is registered and first read. A later
    // setThemeMode re-themes live via the AppViewModel theme-applied sink below.
    {
        const auto mode = model.themeStore()->mode();
        const auto appearance = mode == dish::source::ThemeMode::Light ? dish::ui::Appearance::Light
                                : mode == dish::source::ThemeMode::Dark
                                    ? dish::ui::Appearance::Dark
                                    : dish::ui::detectSystemAppearance();
        dish::ui::setActiveAppearance(appearance);
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
    auto* tokensBridge = new dish::chrome::TokensBridge(qApp);
    QQmlEngine::setObjectOwnership(bridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(themeBridge, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(tokensBridge, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "ChromeBridge", bridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Theme", themeBridge);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Tokens", tokensBridge);

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty(QStringLiteral("App"), &appVm);

    // A shared borrow of the chrome filter so the theme-applied sink (below) can
    // flip the native immersive-dark attribute after the window exists. Filled by
    // the objectCreated handler; null until then.
    auto chromeHolder = std::make_shared<dish::chrome::FramelessWindowChrome*>(nullptr);

    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [bridge, themeBridge, chromeHolder](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) { return; }
            // The chrome filter outlives the window (parented to the app). Mica
            // + the native hit-test attach once the platform window exists.
            auto* chrome = new dish::chrome::FramelessWindowChrome(window, qApp);
            *chromeHolder = chrome;
            qApp->installNativeEventFilter(chrome);
            if (bridge) { bridge->setChrome(chrome); }
            const bool mica = chrome->applyMicaBackdrop();
            // Push the resolved STARTUP appearance so a persisted Light mode paints
            // the solid light background + light chrome from the first frame. The
            // theme-applied sink only fires on a later user toggle, so without this
            // a cold start in Light kept the dark Mica backdrop until the user
            // toggled the theme.
            const bool startupDark = dish::ui::activeAppearance() == dish::ui::Appearance::Dark;
            chrome->setImmersiveDarkMode(startupDark);
            if (bridge) {
                bridge->setMicaActive(mica);
                bridge->setDark(startupDark);
            }
            // Belt-and-braces for the System+dark cold-start body-vs-chrome
            // split: any binding that evaluated before the palette settled
            // re-reads now that the window exists.
            if (themeBridge) { themeBridge->refresh(); }
        },
        Qt::DirectConnection);

    // Route App.openExternalUrl through the shared ExternalLink helper; a false
    // return falls through to App.errorMessage (the QML toast channel).
    appVm.setExternalOpenSink([](const QString& url) { return dish::ui::openExternalUrl(url); });

    // After a theme change: re-read the C++ tokens into the QML Theme singleton
    // and flip the native chrome's immersive-dark attribute so the frame matches.
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
