// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MotionPreferenceRepositoryTest (ADAPT, 6) + the Wave 0 RepositoryContract.
// Port of dish-android repository/MotionPreferenceRepositoryTest: enabled
// true/false round-trip with durability across a fresh repo, get-on-unwritten
// returns nullopt (NOT a default boolean — the store layer owns the default),
// corrupt JSON falls back to empty (no crash), remove of one slot leaves the
// others, and put on the same slot replaces in place (the list never grows).

#include "repository/MotionPreferenceRepository.h"

#include "QSettingsFixture.h"
#include "RepositoryContract.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

using dish::repository::MotionPreference;
using dish::repository::MotionPreferenceRepository;
using dish::test::makeSharedSettings;

TEST_CASE("MotionPreferenceRepository satisfies the repository contract", "[repository][motion]") {
    dish::test::runRepositoryContract<QString, MotionPreference>(
        [] { return std::make_unique<MotionPreferenceRepository>(makeSharedSettings()); },
        [](int i) { return QStringLiteral("slot-%1").arg(i); },
        [](const QString& k) { return MotionPreference{k, (k.size() % 2) == 0}; });
}

TEST_CASE("enabled true and false round-trip with durability", "[motion-pref]") {
    auto store = makeSharedSettings();
    {
        MotionPreferenceRepository repo(store);
        repo.put(MotionPreference{"virtual", true});
        repo.put(MotionPreference{"9", false});
    }
    // Fresh repo on the same backing store proves durability (no in-memory cache).
    MotionPreferenceRepository repo2(store);
    REQUIRE(repo2.get("virtual").has_value());
    CHECK(repo2.get("virtual")->enabled == true);
    REQUIRE(repo2.get("9").has_value());
    CHECK(repo2.get("9")->enabled == false);
}

TEST_CASE("get on a slot that was never written returns nullopt, not a default", "[motion-pref]") {
    MotionPreferenceRepository repo(makeSharedSettings());
    CHECK_FALSE(repo.get("never-written").has_value());
}

TEST_CASE("corrupt JSON falls back to empty, does not crash", "[motion-pref]") {
    auto store = makeSharedSettings();
    store->setValue(QStringLiteral("motion_preferences"), QByteArrayLiteral("{not valid json"));
    MotionPreferenceRepository repo(store);
    CHECK(repo.all().empty());
    CHECK_FALSE(repo.get("anything").has_value());
}

TEST_CASE("remove of one slot does not disturb other slots", "[motion-pref]") {
    MotionPreferenceRepository repo(makeSharedSettings());
    repo.put(MotionPreference{"virtual", true});
    repo.put(MotionPreference{"9", false});
    repo.remove("9");
    REQUIRE(repo.get("virtual").has_value());
    CHECK(repo.get("virtual")->enabled == true);
    CHECK_FALSE(repo.get("9").has_value());
}

TEST_CASE("put with the same slot replaces in place, list never grows", "[motion-pref]") {
    MotionPreferenceRepository repo(makeSharedSettings());
    repo.put(MotionPreference{"virtual", true});
    repo.put(MotionPreference{"virtual", false});
    repo.put(MotionPreference{"virtual", true});
    CHECK(repo.all().size() == 1);
    CHECK(repo.get("virtual")->enabled == true);
}
