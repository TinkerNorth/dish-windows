// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// QFontDatabase::systemFont(FixedFont) resolves to Courier New on many Windows
// installs, so the mono stack is probed explicitly instead. `pickFamily` takes
// the available list as an argument, keeping the ordering rule testable without
// a font database.

#pragma once

#include <QFontDatabase>
#include <QGuiApplication>
#include <QString>
#include <QStringList>

namespace dish::ui {

// Case-insensitive: Windows reports "Consolas", fontconfig may report
// "consolas".
inline QString pickFamily(const QStringList& candidates, const QStringList& available,
                          const QString& fallback) {
    for (const QString& candidate : candidates) {
        for (const QString& family : available) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0) { return family; }
        }
    }
    return fallback;
}

// Never returns Courier New while any of the three candidates is installed.
inline QString preferredMonoFamily() {
    static const QStringList kCandidates{QStringLiteral("Cascadia Mono"),
                                         QStringLiteral("Consolas"),
                                         QStringLiteral("Segoe UI Mono")};
    return pickFamily(kCandidates, QFontDatabase::families(),
                      QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
}

// Inter is bundled; main.cpp registers it from :/fonts.
inline QString preferredSansFamily() {
    static const QStringList kCandidates{QStringLiteral("Inter"),
                                         QStringLiteral("Segoe UI Variable Text"),
                                         QStringLiteral("Segoe UI")};
    return pickFamily(kCandidates, QFontDatabase::families(), QGuiApplication::font().family());
}

} // namespace dish::ui
