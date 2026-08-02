// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// FontStacks — the two font-family probes the Tokens singleton publishes.
//
// The design system asks for "the platform-generic monospace", which the old
// TokensBridge implemented as QFontDatabase::systemFont(FixedFont). On several
// Windows configurations that resolves to **Courier New** — a serif typewriter
// face — and mono here is not decoration: it carries every Hz readout, IP,
// latency and telemetry line ("this is a machine reading"). So the stack is
// probed explicitly, in order, and the FixedFont generic is only the last
// resort.
//
// Header-only + pure: `pickFamily` takes the available list as an argument, so
// the ORDERING rule is unit-testable without a font database (the tests link
// dish_core, not the Quick target where TokensBridge lives).

#pragma once

#include <QFontDatabase>
#include <QGuiApplication>
#include <QString>
#include <QStringList>

namespace dish::ui {

// The first `candidates` entry present in `available` (case-insensitive —
// Windows reports "Consolas", fontconfig may report "consolas"), else
// `fallback`. Pure: no font database is touched.
inline QString pickFamily(const QStringList& candidates, const QStringList& available,
                          const QString& fallback) {
    for (const QString& candidate : candidates) {
        for (const QString& family : available) {
            if (family.compare(candidate, Qt::CaseInsensitive) == 0) { return family; }
        }
    }
    return fallback;
}

// Cascadia Mono -> Consolas -> Segoe UI Mono -> the FixedFont generic. Never
// returns Courier New while any of the three is installed.
inline QString preferredMonoFamily() {
    static const QStringList kCandidates{QStringLiteral("Cascadia Mono"),
                                         QStringLiteral("Consolas"),
                                         QStringLiteral("Segoe UI Mono")};
    return pickFamily(kCandidates, QFontDatabase::families(),
                      QFontDatabase::systemFont(QFontDatabase::FixedFont).family());
}

// Inter (bundled — main.cpp registers it from :/fonts) -> Segoe UI Variable
// Text -> Segoe UI -> whatever the application font already resolved to.
inline QString preferredSansFamily() {
    static const QStringList kCandidates{QStringLiteral("Inter"),
                                         QStringLiteral("Segoe UI Variable Text"),
                                         QStringLiteral("Segoe UI")};
    return pickFamily(kCandidates, QFontDatabase::families(), QGuiApplication::font().family());
}

} // namespace dish::ui
