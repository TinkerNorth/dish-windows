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
    // FIRST, before any other subsystem can fault, so a crash still leaves a
    // minidump behind.
    dish::crash::install();

    // Every network call assumes Winsock is up; this RAII guard holds it for
    // the lifetime of `main`.
    dish::net::WinsockInit winsock;
    if (!winsock.ok()) {
        // Discarded deliberately: this runs before any logger exists, so a
        // failed write to stderr is unactionable. The exit code is the report.
        (void)std::fprintf(stderr, "dish: WSAStartup failed\n");
        return 1;
    }

    if (sodium_init() < 0) {
        (void)std::fprintf(stderr, "dish: libsodium initialisation failed\n");
        return 1;
    }

    // QGuiApplication, not QApplication: no QWidget is ever constructed, so the
    // widgets module stays out of the process.
    QGuiApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TinkerNorth"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tinkernorth.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Dish"));

    // loadCatalog walks QLocale::uiLanguages(), so Windows' preferred UI
    // language wins over the regional format setting — two settings that
    // routinely disagree. English is a real catalogue rather than the
    // untranslated fallback, because %n plural forms have to come from
    // somewhere and a source string can only carry one of them. `static` keeps
    // the translator alive for the lifetime of the app.
    static QTranslator translator;
    if (dish::i18n::loadCatalog(translator, QLocale::system())) {
        QCoreApplication::installTranslator(&translator);
    }

    // dish.rc embeds the icon into the PE resource section, which Qt cannot
    // see: without this the window shows Qt's generic icon in Alt-Tab and the
    // running taskbar. QIcon picks the best size per DPI from the same .ico.
    app.setWindowIcon(QIcon(QStringLiteral(":/dish.ico")));

    // Inter is bundled (SIL OFL, see packaging/fonts/) because Windows does not
    // ship it, and without the load every Text falls back to Segoe UI. The four
    // statics give the weight ladder the tokens use.
    for (const char* face : {":/fonts/Inter-Regular.ttf", ":/fonts/Inter-Medium.ttf",
                             ":/fonts/Inter-SemiBold.ttf", ":/fonts/Inter-Bold.ttf"}) {
        QFontDatabase::addApplicationFont(QLatin1String(face));
    }
    QFont uiFont(QStringLiteral("Inter"));
    uiFont.setPixelSize(13); // the token base; pages override per role
    app.setFont(uiFont);

    // runQmlApp owns the engine and chrome, and exposes the model to QML as the
    // `App` context property.
    dish::AppModel model;
    model.start();
    return dish::qml::runQmlApp(model);
}
