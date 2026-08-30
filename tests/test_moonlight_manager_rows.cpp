// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pure host-row merge the Moonlight manager surfaces to the UI, and the
// phase-token mapping.

#include "Network/MoonlightManager.h"

#include <catch2/catch_test_macros.hpp>

using namespace dish::net;
using dish::models::MoonlightHost;

namespace {
MoonlightHost host(const QString& name, const QString& ip, const QString& uuid = QString(),
                   bool paired = false) {
    MoonlightHost h;
    h.name = name;
    h.ip = ip;
    h.uuid = uuid;
    h.paired = paired;
    return h;
}
} // namespace

TEST_CASE("moonlightPhaseToken maps every phase", "[moonlight][manager]") {
    REQUIRE(moonlightPhaseToken(dish::moonlight::SessionPhase::Idle) == QStringLiteral("idle"));
    REQUIRE(moonlightPhaseToken(dish::moonlight::SessionPhase::Streaming) ==
            QStringLiteral("streaming"));
    REQUIRE(moonlightPhaseToken(dish::moonlight::SessionPhase::RtspHandshake) ==
            QStringLiteral("connecting"));
    REQUIRE(moonlightPhaseToken(dish::moonlight::SessionPhase::ControlConnecting) ==
            QStringLiteral("connecting"));
    REQUIRE(moonlightPhaseToken(dish::moonlight::SessionPhase::Failed) == QStringLiteral("failed"));
}

TEST_CASE("mergeMoonlightRows lists remembered first, folds a discovered dup",
          "[moonlight][manager]") {
    const QList<MoonlightHost> remembered = {host("PC-A", "10.0.0.2", "uuid-a", true)};
    // Same host rediscovered (same uuid) + a brand-new one.
    const QList<MoonlightHost> discovered = {host("PC-A", "10.0.0.2", "uuid-a"),
                                             host("PC-B", "10.0.0.3")};
    QHash<QString, QString> phases;
    phases.insert(host("PC-A", "10.0.0.2", "uuid-a").id(), QStringLiteral("streaming"));

    const auto rows = mergeMoonlightRows(remembered, discovered, phases);
    REQUIRE(rows.size() == 2); // the dup folded, not duplicated

    // Remembered PC-A first, marked discovered now, with its live phase.
    REQUIRE(rows[0].name == QStringLiteral("PC-A"));
    REQUIRE(rows[0].paired);
    REQUIRE(rows[0].discovered);
    REQUIRE(rows[0].phaseToken == QStringLiteral("streaming"));

    // PC-B is discovery-only and idle.
    REQUIRE(rows[1].name == QStringLiteral("PC-B"));
    REQUIRE_FALSE(rows[1].paired);
    REQUIRE(rows[1].discovered);
    REQUIRE(rows[1].phaseToken == QStringLiteral("idle"));
}

TEST_CASE("mergeMoonlightRows falls back to IP for a nameless host", "[moonlight][manager]") {
    const QList<MoonlightHost> discovered = {host(QString(), "192.168.1.9")};
    const auto rows = mergeMoonlightRows({}, discovered, {});
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].name == QStringLiteral("192.168.1.9"));
}
