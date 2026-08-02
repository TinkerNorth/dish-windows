// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// FontStacks — the ORDERING rule behind Tokens.monoFamily / Tokens.sansFamily.
//
// The bug this exists to prevent: asking Qt for the platform FixedFont generic
// resolves to Courier New on several Windows configurations, and mono is not
// decoration here — it carries every Hz readout, IP, latency and telemetry line
// ("this is a machine reading"). A serif typewriter face at 10 px would wreck
// the instrument-panel voice, and nothing would catch it.
//
// pickFamily takes the available list as an argument, so the rule is testable
// against a synthetic font database — no real installed fonts are consulted and
// the test is identical on every machine. (TokensBridge itself lives in the Quick
// target and cannot be linked here; that is exactly why the probe is a free
// function in dish_core.)

#include "UI/FontStacks.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>
#include <QStringList>

using dish::ui::pickFamily;

TEST_CASE("pickFamily returns the first CANDIDATE present, not the first available",
          "[fonts][stacks]") {
    // Preference order is the candidate list's, never the font database's.
    const QStringList candidates{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")};
    const QStringList available{QStringLiteral("Consolas"), QStringLiteral("Cascadia Mono"),
                                QStringLiteral("Arial")};
    REQUIRE(pickFamily(candidates, available, QStringLiteral("Courier New")) ==
            QStringLiteral("Cascadia Mono"));
}

TEST_CASE("pickFamily walks down the stack when the preferred family is missing",
          "[fonts][stacks]") {
    const QStringList candidates{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                                 QStringLiteral("Segoe UI Mono")};
    REQUIRE(pickFamily(candidates, {QStringLiteral("Consolas"), QStringLiteral("Segoe UI Mono")},
                       QStringLiteral("Courier New")) == QStringLiteral("Consolas"));
    REQUIRE(pickFamily(candidates, {QStringLiteral("Segoe UI Mono")},
                       QStringLiteral("Courier New")) == QStringLiteral("Segoe UI Mono"));
}

TEST_CASE("pickFamily falls back only when no candidate is installed", "[fonts][stacks]") {
    const QStringList candidates{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas")};
    REQUIRE(pickFamily(candidates, {QStringLiteral("Arial"), QStringLiteral("Courier New")},
                       QStringLiteral("Courier New")) == QStringLiteral("Courier New"));
    // An empty database is the degenerate case, not a crash.
    REQUIRE(pickFamily(candidates, {}, QStringLiteral("Courier New")) ==
            QStringLiteral("Courier New"));
    // ...and an empty candidate list always falls back.
    REQUIRE(pickFamily({}, {QStringLiteral("Consolas")}, QStringLiteral("Courier New")) ==
            QStringLiteral("Courier New"));
}

TEST_CASE("pickFamily matches case-insensitively and keeps the database spelling",
          "[fonts][stacks]") {
    // Windows reports "Consolas"; a fontconfig-backed platform may report
    // "consolas". Either must match, and the returned name must be the one the
    // font database actually knows.
    REQUIRE(pickFamily({QStringLiteral("Consolas")}, {QStringLiteral("consolas")},
                       QStringLiteral("Courier New")) == QStringLiteral("consolas"));
}

TEST_CASE("the mono stack never yields Courier New while a real mono is installed",
          "[fonts][stacks]") {
    // THE regression this file exists for. Whatever the platform generic would
    // have said, an installed Consolas wins.
    const QStringList candidates{QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"),
                                 QStringLiteral("Segoe UI Mono")};
    const QStringList available{QStringLiteral("Courier New"), QStringLiteral("Consolas"),
                                QStringLiteral("Times New Roman")};
    const QString picked = pickFamily(candidates, available, QStringLiteral("Courier New"));
    REQUIRE(picked != QStringLiteral("Courier New"));
    REQUIRE(picked == QStringLiteral("Consolas"));
}

TEST_CASE("the sans stack prefers the bundled Inter over the system face", "[fonts][stacks]") {
    // Inter ships in the qrc and is registered at startup, so it is present on
    // every machine; the Segoe rungs are the fallback for a build without it.
    const QStringList candidates{QStringLiteral("Inter"), QStringLiteral("Segoe UI Variable Text"),
                                 QStringLiteral("Segoe UI")};
    REQUIRE(pickFamily(candidates,
                       {QStringLiteral("Segoe UI"), QStringLiteral("Inter"),
                        QStringLiteral("Segoe UI Variable Text")},
                       QStringLiteral("MS Shell Dlg 2")) == QStringLiteral("Inter"));
    REQUIRE(pickFamily(
                candidates, {QStringLiteral("Segoe UI"), QStringLiteral("Segoe UI Variable Text")},
                QStringLiteral("MS Shell Dlg 2")) == QStringLiteral("Segoe UI Variable Text"));
}
