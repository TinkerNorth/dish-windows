// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Locks the pure FOUND-list visibility mapper (the Connections page's one-spot
// rule): a discovered satellite whose stable id already has a row in the
// derived connections list (remembered ∪ live) is NOT offered in FOUND — that
// row is its one spot; FOUND carries only the un-remembered rest. The mapper is
// the decision core of AppViewModel::discoveredServers() (which can't be
// instantiated in this harness — it owns the full AppModel graph), so these pin
// the rule at the seam: identity keying (machineId-preferring, so a DHCP move
// still folds), mid-pair live-session suppression, forget-reappearance, and
// scan-order preservation. Pure, no Qt widgets, no IO.

#include "Models/Models.h"
#include "core/reducer/FoundVisibility.h"

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QSet>
#include <QString>

using dish::models::DiscoveredServer;
using dish::reducer::serversVisibleInFound;

namespace {

DiscoveredServer sat(const QString& machineId, const QString& ip) {
    DiscoveredServer s;
    s.machineId = machineId;
    s.ip = ip;
    s.name = QStringLiteral("Box %1").arg(machineId.isEmpty() ? ip : machineId);
    return s;
}

QList<QString> ids(const QList<DiscoveredServer>& list) {
    QList<QString> out;
    for (const auto& s : list) { out.push_back(s.id()); }
    return out;
}

} // namespace

TEST_CASE("a discovered satellite with a connections row is not offered in FOUND",
          "[reducer][found][onespot]") {
    // The user's report: one physical box must never render as BOTH a FOUND row
    // and a REMEMBERED row. Its connections row (chip Ready/Online/…) wins.
    const auto discovered = QList<DiscoveredServer>{sat(QStringLiteral("m1"), "10.0.0.2"),
                                                    sat(QStringLiteral("m2"), "10.0.0.3")};
    const QSet<QString> rowIds{QStringLiteral("mid:m1")};

    const auto visible = serversVisibleInFound(discovered, rowIds);

    CHECK(ids(visible) == QList<QString>{QStringLiteral("mid:m2")});
}

TEST_CASE("an empty connections list leaves the whole scan in FOUND, in scan order",
          "[reducer][found][order]") {
    const auto discovered = QList<DiscoveredServer>{sat(QStringLiteral("m2"), "10.0.0.3"),
                                                    sat(QStringLiteral("m1"), "10.0.0.2")};

    const auto visible = serversVisibleInFound(discovered, {});

    // Nothing remembered, nothing live: FOUND is the raw scan, order preserved
    // (the scan order IS the row order — the mapper never re-sorts).
    CHECK(ids(visible) == QList<QString>{QStringLiteral("mid:m2"), QStringLiteral("mid:m1")});
}

TEST_CASE("identity keys on the stable machineId, so a DHCP move still folds",
          "[reducer][found][identity]") {
    // The remembered row was learned at 10.0.0.2; the box re-appears in a fresh
    // scan at .250. Same machineId → same stable id → still only one spot.
    const auto moved = sat(QStringLiteral("m1"), QStringLiteral("10.0.0.250"));
    const QSet<QString> rowIds{QStringLiteral("mid:m1")};

    CHECK(serversVisibleInFound({moved}, rowIds).isEmpty());
}

TEST_CASE("a legacy machineId-less satellite folds on its ip:udpPort fallback id",
          "[reducer][found][identity]") {
    DiscoveredServer legacy;
    legacy.ip = QStringLiteral("10.0.0.5");
    legacy.udpPort = 9876;
    REQUIRE(legacy.id() == QStringLiteral("wifi:10.0.0.5:9876"));

    const QSet<QString> rowIds{QStringLiteral("wifi:10.0.0.5:9876")};
    CHECK(serversVisibleInFound({legacy}, rowIds).isEmpty());

    // …but a different port is a different legacy identity: still FOUND.
    DiscoveredServer otherPort = legacy;
    otherPort.udpPort = 9877;
    CHECK(ids(serversVisibleInFound({otherPort}, rowIds)) ==
          QList<QString>{QStringLiteral("wifi:10.0.0.5:9877")});
}

TEST_CASE("a mid-pair live session (not yet remembered) already suppresses its FOUND row",
          "[reducer][found][midpair]") {
    // The row-id universe is remembered ∪ LIVE — a session that exists before
    // the first PUT persists it already has a connections row, so keying the
    // filter on row ids (not remembered ids) keeps the one-spot rule through
    // the whole pairing lifecycle.
    const auto pairing = sat(QStringLiteral("m9"), QStringLiteral("10.0.0.9"));
    const QSet<QString> rowIds{pairing.id()}; // the live row, no remembered row yet

    CHECK(serversVisibleInFound({pairing}, rowIds).isEmpty());
}

TEST_CASE("forget drops the row id and the box re-earns its FOUND row",
          "[reducer][found][forget]") {
    const auto box = sat(QStringLiteral("m1"), QStringLiteral("10.0.0.2"));

    // Remembered: hidden from FOUND…
    CHECK(serversVisibleInFound({box}, {box.id()}).isEmpty());
    // …forgotten (row id gone): back in FOUND so the user can re-pair it.
    CHECK(ids(serversVisibleInFound({box}, {})) == QList<QString>{box.id()});
}

TEST_CASE("the input scan list is never mutated", "[reducer][found][pure]") {
    const auto discovered = QList<DiscoveredServer>{sat(QStringLiteral("m1"), "10.0.0.2"),
                                                    sat(QStringLiteral("m2"), "10.0.0.3")};
    const auto before = ids(discovered);

    (void)serversVisibleInFound(discovered, {QStringLiteral("mid:m1")});

    CHECK(ids(discovered) == before);
}
