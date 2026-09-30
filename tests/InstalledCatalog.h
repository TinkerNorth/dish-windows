// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A compiled catalogue installed on the application for one test's lifetime,
// the way main.cpp installs the user's, so a test can assert that a message the
// code raises actually reaches someone in their own language.

#pragma once

#include "Util/Localization.h"

#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QString>
#include <QTranslator>

namespace dish::test {

// A build without the Qt Linguist tools has no .qm to read.
inline bool catalogsBuilt() { return QFile::exists(QStringLiteral(DISH_QM_DIR "/dish_de.qm")); }

// Catalogue lookups need a QCoreApplication; Catch2WithMain creates none. The
// function-local static with a leaked argv keeps one alive for the process.
inline void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

struct InstalledCatalog {
    QTranslator translator;
    bool loaded = false;

    explicit InstalledCatalog(const QString& localeName) {
        ensureApp();
        loaded =
            dish::i18n::loadCatalog(translator, QLocale(localeName), QStringLiteral(DISH_QM_DIR));
        if (loaded) { QCoreApplication::installTranslator(&translator); }
    }
    ~InstalledCatalog() { QCoreApplication::removeTranslator(&translator); }
    InstalledCatalog(const InstalledCatalog&) = delete;
    InstalledCatalog& operator=(const InstalledCatalog&) = delete;
    InstalledCatalog(InstalledCatalog&&) = delete;
    InstalledCatalog& operator=(InstalledCatalog&&) = delete;

    // The catalogue's text for `source` under `context`, empty when it has none.
    QString lookup(const char* context, const QString& source) const {
        return translator.translate(context, source.toUtf8().constData());
    }
};

} // namespace dish::test
