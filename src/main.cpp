// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "AppModel.h"
#include "Network/WinsockInit.h"
#include "UI/CrashHandler.h"
#include "Util/Localization.h"
#include "qml/QmlEntryPoint.h"

#include <QFont>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QTranslator>

#include <sodium.h>

#include <cstdio>

int main(int argc, char* argv[]) {
    // Arm crash diagnostics FIRST — before any other subsystem can fault — so an
    // unhandled SEH (access violation, etc.) or a debug-CRT assert writes a
    // minidump + a symbolized crash.log to %LOCALAPPDATA%\Dish\ for the user to
    // send. Dependency-light and self-guarding; see UI/CrashHandler.cpp.
    dish::crash::install();

    // Initialize Winsock for the lifetime of `main`. Every network call in
    // the app (LANDiscovery, PairingClient, the per-session SatelliteClient
    // threads) assumes Winsock is up; this RAII guard guarantees it. The
    // POSIX siblings have no equivalent — sockets just work.
    dish::net::WinsockInit winsock;
    if (!winsock.ok()) {
        // The `(void)` is deliberate. fprintf's result is a byte count / negative
        // error, and a failed write to stderr here is unactionable: this runs
        // before any logger exists and there is no second channel to complain on.
        // The non-zero exit code below is what actually reports the failure.
        (void)std::fprintf(stderr, "dish: WSAStartup failed\n");
        return 1;
    }

    if (sodium_init() < 0) {
        // Discarded for the same reason as the WSAStartup message above.
        (void)std::fprintf(stderr, "dish: libsodium initialisation failed\n");
        return 1;
    }

    // QGuiApplication, not QApplication: the app is Qt Quick only — no QWidget
    // is ever constructed, so the widgets module stays out of the process.
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TinkerNorth"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tinkernorth.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Dish"));
    // No setDesktopFileName on Windows — that's an XDG/Linux concept.

    // i18n: load the .qm matching the user's language out of the QRC-embedded
    // `:/i18n/dish_<locale>.qm` family. dish::i18n::loadCatalog walks
    // QLocale::uiLanguages() so Windows' *preferred UI language* wins over the
    // regional format setting, which are two different settings that routinely
    // disagree. English is a catalogue like any other (dish_en.qm) rather than
    // the untranslated fallback, because %n plural forms have to come from
    // somewhere and the source string can only carry one of them. The
    // translator stays alive for the lifetime of the app via `static`.
    static QTranslator translator;
    if (dish::i18n::loadCatalog(translator, QLocale::system())) {
        QCoreApplication::installTranslator(&translator);
    }

    // dish.rc embeds the icon into the PE resource section (Explorer / Task
    // Manager / taskbar-from-pinned). That's invisible to Qt — without
    // setWindowIcon the window shows Qt's generic icon in Alt-Tab and the
    // taskbar-while-running. dish.qrc ships the same multi-resolution .ico via
    // AUTORCC; QIcon picks the best size per DPI.
    app.setWindowIcon(QIcon(QStringLiteral(":/dish.ico")));

    // The design system's UI face. Inter is bundled (SIL OFL; see
    // packaging/fonts/) because Windows does not ship it — without the load
    // every Text fell back to Segoe UI and the app visibly diverged from the
    // design canvas. Registering the four statics gives the weight ladder the
    // tokens use (400/500/600/700); the app default font then propagates to
    // every Quick Text that doesn't set its own family.
    for (const char* face : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf",
                             ":/fonts/Inter-SemiBold.ttf", ":/fonts/Inter-Bold.ttf"}) {
        QFontDatabase::addApplicationFont(QLatin1String(face));
    }
    QFont uiFont(QStringLiteral("Inter"));
    uiFont.setPixelSize(13); // the token base; pages override per role
    app.setFont(uiFont);

    // The AppModel is exposed to QML as the `App` context property (an
    // AppViewModel adapter) inside runQmlApp, which owns the engine + chrome.
    dish::AppModel model;
    model.start();
    return dish::qml::runQmlApp(model);
}
