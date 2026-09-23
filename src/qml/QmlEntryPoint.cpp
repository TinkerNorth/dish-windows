// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/QmlEntryPoint.h"

#include "AppModel.h"
#include "qml/AppViewModel.h"
#include "qml/ConnectionListModel.h"
#include "qml/SlotListModel.h"
#include "qml/chrome/ChromeBridge.h"
#include "qml/chrome/ForeignTypes.h"
#include "qml/chrome/FramelessWindowChrome.h"
#include "qml/chrome/ThemeBridge.h"
#include "qml/chrome/TokensBridge.h"
#include "UI/common/ExternalLink.h"

#include "UI/Theme.h"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QWindow>
#include <qqml.h>

#include <memory>

namespace dish::qml {

namespace {

// Uncreatable: instances are only ever vended through App.slotModel / App.connectionModel, but QML
// must be able to name the type in a delegate. By instance for the same LTCG reason the singletons
// below are; the matching QML_FOREIGN declarations in ForeignTypes.h are what put the two names
// into the module's qmltypes for qmllint.
void registerListModelTypes() {
    qmlRegisterUncreatableType<dish::qml::SlotListModel>(
        "Dish.Chrome", 1, 0, "SlotListModel",
        QStringLiteral("SlotListModel is owned by AppViewModel"));
    qmlRegisterUncreatableType<dish::qml::ConnectionListModel>(
        "Dish.Chrome", 1, 0, "ConnectionListModel",
        QStringLiteral("ConnectionListModel is owned by AppViewModel"));
}

// model.start() already ran the ThemeController, but depending on who ran last a System+dark cold
// start painted the body light under a dark title bar. Re-resolving here unconditionally
// (idempotent) makes the active palette provably match the mode before the Theme singleton is first
// read.
void resolveStartupAppearance(dish::AppModel& model) {
    const auto mode = model.themeStore()->mode();
    const auto appearance = mode == dish::source::ThemeMode::Light ? dish::ui::Appearance::Light
                            : mode == dish::source::ThemeMode::Dark
                                ? dish::ui::Appearance::Dark
                                : dish::ui::detectSystemAppearance();
    dish::ui::setActiveAppearance(appearance);
}

// The two the window wiring needs later. Tokens is registered and never referred to again.
struct ChromeBridges {
    dish::chrome::ChromeBridge* chrome = nullptr;
    dish::chrome::ThemeBridge* theme = nullptr;
};

// Registered BY INSTANCE, not via QML_SINGLETON: under this target's LTCG (/GL) the generated
// QQmlModuleRegistration static initializer is stripped, so the auto-registered names never reach
// the engine and every `Theme.*` / `ChromeBridge.*` reference becomes a ReferenceError - leaving the
// window at QtQuick's default white. Parented to qApp so they outlive the engine; CppOwnership so
// QML never deletes them.
ChromeBridges registerChromeSingletons() {
    ChromeBridges bridges{new dish::chrome::ChromeBridge(qApp),
                          new dish::chrome::ThemeBridge(qApp)};
    auto* tokensBridge = new dish::chrome::TokensBridge(qApp);
    QQmlEngine::setObjectOwnership(bridges.chrome, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(bridges.theme, QQmlEngine::CppOwnership);
    QQmlEngine::setObjectOwnership(tokensBridge, QQmlEngine::CppOwnership);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "ChromeBridge", bridges.chrome);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Theme", bridges.theme);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "Tokens", tokensBridge);
    return bridges;
}

// `App` is a module singleton, not a context property: a context property is invisible to qmllint,
// so every `App.x` in the QML read as an unqualified access and the whole category had to be
// downgraded. It is registered by instance like the three above, and ForeignTypes.h declares the
// same name declaratively so it reaches the qmltypes; that declaration vends THIS object too, so
// the two registrations cannot disagree. The caller's appVm outlives the engine, and CppOwnership
// keeps QML from ever deleting a stack object.
void registerAppSingleton(dish::qml::AppViewModel& appVm) {
    QQmlEngine::setObjectOwnership(&appVm, QQmlEngine::CppOwnership);
    dish::chrome::AppViewModelForeign::setInstance(&appVm);
    qmlRegisterSingletonInstance("Dish.Chrome", 1, 0, "App", &appVm);
}

// The chrome exists only once the window does, so `chromeHolder` is what the sinks below borrow it
// through. Null until this fires.
void wireWindowChrome(QQmlApplicationEngine& engine, ChromeBridges bridges,
                      std::shared_ptr<dish::chrome::FramelessWindowChrome*> chromeHolder) {
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreated, qApp,
        [bridges, chromeHolder](QObject* obj, const QUrl&) {
            auto* window = qobject_cast<QQuickWindow*>(obj);
            if (!window) { return; }
            // Parented to the app, not the window: it must survive the messages that still pump
            // while the engine tears the window down.
            auto* chrome = new dish::chrome::FramelessWindowChrome(window, qApp);
            *chromeHolder = chrome;
            qApp->installNativeEventFilter(chrome);
            if (bridges.chrome) { bridges.chrome->setChrome(chrome); }
            const bool mica = chrome->applyMicaBackdrop();
            // The theme-applied sink only fires on a later user toggle, so the STARTUP appearance
            // has to be pushed here or a cold start in Light keeps the dark Mica backdrop until the
            // user toggles the theme.
            const bool startupDark = dish::ui::activeAppearance() == dish::ui::Appearance::Dark;
            chrome->setImmersiveDarkMode(startupDark);
            if (bridges.chrome) {
                bridges.chrome->setMicaActive(mica);
                bridges.chrome->setDark(startupDark);
            }
            // Any binding that evaluated before the palette settled re-reads now that the window
            // exists.
            if (bridges.theme) { bridges.theme->refresh(); }
        },
        Qt::DirectConnection);
}

void wireViewModelSinks(dish::qml::AppViewModel& appVm, ChromeBridges bridges,
                        std::shared_ptr<dish::chrome::FramelessWindowChrome*> chromeHolder) {
    // A false return falls through to App.errorMessage (the QML toast channel).
    appVm.setExternalOpenSink([](const QString& url) { return dish::ui::openExternalUrl(url); });

    // The frame must re-theme with the body, or it drifts light while the body re-darks.
    appVm.setThemeAppliedSink([bridges, chromeHolder](bool dark) {
        if (bridges.theme) { bridges.theme->refresh(); }
        if (bridges.chrome) {
            // Drives Main.qml's transparent-vs-solid background: a light app over a dark desktop
            // must not keep the dark Mica backdrop showing through.
            bridges.chrome->setDark(dark);
        }
        if (*chromeHolder) { (*chromeHolder)->setImmersiveDarkMode(dark); }
    });
}

} // namespace

int runQmlApp(dish::AppModel& model) {
    // FluentWinUI3 needs Qt 6.8 and is unavailable here; the Win11 look comes from the native
    // chrome filter, not the style.
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    // Closing the window is allowed to mean "keep streaming", so the process must outlive it.
    // Main.qml's approveClose() is the one real quit path.
    QGuiApplication::setQuitOnLastWindowClosed(false);

    registerListModelTypes();

    // Declared before the engine so it outlives it.
    dish::qml::AppViewModel appVm(&model);

    resolveStartupAppearance(model);
    const ChromeBridges bridges = registerChromeSingletons();
    registerAppSingleton(appVm);

    QQmlApplicationEngine engine;
    auto chromeHolder = std::make_shared<dish::chrome::FramelessWindowChrome*>(nullptr);
    wireWindowChrome(engine, bridges, chromeHolder);
    wireViewModelSinks(appVm, bridges, chromeHolder);

    engine.loadFromModule(QStringLiteral("Dish.Chrome"), QStringLiteral("Main"));
    if (engine.rootObjects().isEmpty()) { return 1; }

    return QGuiApplication::exec();
}

} // namespace dish::qml
