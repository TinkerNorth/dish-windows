// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The navigation rail's collapsed state, which is persisted schema: renaming
// ui_rail_collapsed silently expands the rail again for every user who had
// collapsed it, and nothing else would fail.
//
// The re-emit rule is the other half. The shell re-renders on every publish, so
// a store that republished an unchanged value would repaint the whole shell each
// time a caller re-asserted what was already true.
//
// Every case runs against a temp INI through the injecting constructor, so no
// test here touches the user's real settings.

#include "source/store/UiPreferenceStore.h"

#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>

using dish::source::UiPreferences;
using dish::source::UiPreferenceStore;
using dish::test::StateSourceProbe;

namespace {

std::unique_ptr<UiPreferenceStore> makeStore(const QString& iniPath) {
    return std::make_unique<UiPreferenceStore>(
        std::make_unique<QSettings>(iniPath, QSettings::IniFormat));
}

} // namespace

TEST_CASE("ui preference store: the key name is a persisted schema", "[settings][ui]") {
    CHECK(QString::fromLatin1(UiPreferenceStore::kKeyRailCollapsed) ==
          QStringLiteral("ui_rail_collapsed"));
}

TEST_CASE("ui preference store: a fresh store shows the rail", "[settings][ui]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto store = makeStore(dir.filePath(QStringLiteral("ui.ini")));
    CHECK_FALSE(store->railCollapsed());
    // A fresh file must read back as the struct's own default, so the two
    // cannot drift apart.
    CHECK(store->state().value() == UiPreferences{});
}

TEST_CASE("ui preference store: the collapsed rail survives a reopen", "[settings][ui]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString ini = dir.filePath(QStringLiteral("ui.ini"));
    {
        const auto store = makeStore(ini);
        store->setRailCollapsed(true);
        CHECK(store->railCollapsed());
    }
    // Written under the pinned key, and read back by a store that never saw the
    // setter run.
    QSettings raw(ini, QSettings::IniFormat);
    CHECK(raw.value(QLatin1String(UiPreferenceStore::kKeyRailCollapsed)).toBool());
    CHECK(makeStore(ini)->railCollapsed());
}

TEST_CASE("ui preference store: a flip publishes once", "[settings][ui]") {
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto store = makeStore(dir.filePath(QStringLiteral("ui.ini")));
    StateSourceProbe<UiPreferences> probe(store->state());
    REQUIRE(probe.count() == 1); // the current value, on subscribe

    store->setRailCollapsed(true);
    CHECK(probe.count() == 2);
    CHECK(probe.latest().railCollapsed);

    store->setRailCollapsed(false);
    CHECK(probe.count() == 3);
    CHECK_FALSE(probe.latest().railCollapsed);
}

TEST_CASE("ui preference store: re-asserting the same value publishes nothing", "[settings][ui]") {
    // The shell re-renders on every publish. Toggling a rail that is already
    // collapsed is a no-op the caller is entitled to make, and it must not cost
    // a repaint.
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const auto store = makeStore(dir.filePath(QStringLiteral("ui.ini")));
    StateSourceProbe<UiPreferences> probe(store->state());
    const auto before = probe.count();

    store->setRailCollapsed(false); // already false
    CHECK(probe.count() == before);

    store->setRailCollapsed(true);
    const auto afterFlip = probe.count();
    store->setRailCollapsed(true); // already true
    store->setRailCollapsed(true);
    CHECK(probe.count() == afterFlip);
}
