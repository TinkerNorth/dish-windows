// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "AppModel.h"
#include "Network/WinsockInit.h"
#include "UI/MainWindow.h"
#include "UI/Theme.h"

#include <QApplication>
#include <QIcon>

#include <sodium.h>

#include <cstdio>

int main(int argc, char* argv[]) {
    // Initialize Winsock for the lifetime of `main`. Every network call in
    // the app (LANDiscovery, PairingClient, the per-session SatelliteClient
    // threads) assumes Winsock is up; this RAII guard guarantees it. The
    // POSIX siblings have no equivalent — sockets just work.
    dish::net::WinsockInit winsock;
    if (!winsock.ok()) {
        std::fprintf(stderr, "dish: WSAStartup failed\n");
        return 1;
    }

    if (sodium_init() < 0) {
        std::fprintf(stderr, "dish: libsodium initialisation failed\n");
        return 1;
    }

    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("TinkerNorth"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("tinkernorth.dev"));
    QCoreApplication::setApplicationName(QStringLiteral("Dish"));
    // No setDesktopFileName on Windows — that's an XDG/Linux concept.

    // dish.rc embeds the icon into the PE resource section (Explorer / Task
    // Manager / taskbar-from-pinned). That's invisible to Qt — without
    // setWindowIcon every QWidget shows Qt's generic icon in the title
    // bar, Alt-Tab, and taskbar-while-running. dish.qrc ships the same
    // multi-resolution .ico via AUTORCC; QIcon picks the best size per DPI.
    app.setWindowIcon(QIcon(QStringLiteral(":/dish.ico")));

    dish::ui::applyDishTheme(app);

    dish::AppModel model;
    dish::ui::MainWindow window(&model);
    window.show();
    model.start();

    return app.exec();
}
