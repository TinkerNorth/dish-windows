// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The shortcut seam: the spec the reducer builds (target, working dir, icon,
// description — spec section 10), the location/scope token vocabulary the
// journal writes, and the create/exists/remove lifecycle. Specs carry a
// LOCATION rather than a resolved folder, which is exactly what keeps the
// reducer pure and lets a fake stand in for the shell here.

#include "installer/InstallMachine.h"
#include "installer/InstallPlan.h"
#include "installer/ops/ShortcutOps.h"

#include "installer/FakeOps.h"

#include <catch2/catch_test_macros.hpp>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include <variant>
#include <vector>

using dish::installer::InstallEffect;
using dish::installer::InstallPhase;
using dish::installer::InstallPlan;
using dish::installer::InstallState;
using dish::installer::PayloadEntry;
using dish::installer::PayloadManifest;
using dish::installer::reduce;
using dish::installer::Scope;
using dish::installer::ShortcutLocation;
using dish::installer::shortcutLocationFromToken;
using dish::installer::shortcutLocationToken;
using dish::installer::ShortcutSpec;
using dish::test::FakeShortcutOps;
namespace ev = dish::installer::event;
namespace fx = dish::installer::effect;

namespace {

ShortcutSpec spec(ShortcutLocation location, Scope scope = Scope::PerUser) {
    ShortcutSpec s;
    s.location = location;
    s.scope = scope;
    s.targetAbs = QStringLiteral("C:/App/dish.exe");
    s.workingDir = QStringLiteral("C:/App");
    s.iconAbs = QStringLiteral("C:/App/dish.exe");
    s.iconIndex = 0;
    s.description = QStringLiteral("Dish");
    return s;
}

PayloadManifest manifest() {
    PayloadManifest m;
    m.version = QStringLiteral("1.0.0");
    PayloadEntry e;
    e.path = QStringLiteral("dish.exe");
    e.stagedAs = e.path;
    e.size = 4;
    e.sha256Hex = QByteArray(64, 'c');
    m.files = QVector<PayloadEntry>{e};
    m.totalBytes = 4;
    return m;
}

std::vector<ShortcutSpec> specsFrom(const std::vector<InstallEffect>& effects) {
    std::vector<ShortcutSpec> specs;
    for (const InstallEffect& effect : effects) {
        if (const auto* create = std::get_if<fx::CreateShortcut>(&effect)) {
            specs.push_back(create->spec);
        }
    }
    return specs;
}

} // namespace

TEST_CASE("installer shortcuts: the location tokens are the journal's vocabulary",
          "[installer][shortcuts]") {
    CHECK(shortcutLocationToken(ShortcutLocation::StartMenu) == QStringLiteral("startmenu"));
    CHECK(shortcutLocationToken(ShortcutLocation::Desktop) == QStringLiteral("desktop"));
    CHECK(shortcutLocationFromToken(QStringLiteral("startmenu")) == ShortcutLocation::StartMenu);
    CHECK(shortcutLocationFromToken(QStringLiteral("desktop")) == ShortcutLocation::Desktop);
    // Exact match only: an undo must never guess at which link it is deleting.
    CHECK_FALSE(shortcutLocationFromToken(QStringLiteral("StartMenu")).has_value());
    CHECK_FALSE(shortcutLocationFromToken(QStringLiteral("start menu")).has_value());
    CHECK_FALSE(shortcutLocationFromToken(QString()).has_value());
}

TEST_CASE("installer shortcuts: the reducer builds the section 10 link fields",
          "[installer][shortcuts]") {
    InstallPlan p;
    p.installDir = QStringLiteral("C:/App");
    p.desktop = true;

    InstallState idle;
    idle.phase = InstallPhase::Idle;
    idle.plan = p;
    const auto specs = specsFrom(reduce(idle, ev::Begin{p, manifest()}).effects);

    REQUIRE(specs.size() == 2);
    CHECK(specs.at(0) == spec(ShortcutLocation::StartMenu));
    CHECK(specs.at(1) == spec(ShortcutLocation::Desktop));
    // Target and icon are the same file; the icon index is the exe's first
    // resource, which is what "dish.exe,0" means in ARP's DisplayIcon too.
    CHECK(specs.at(0).targetAbs == QStringLiteral("C:/App/dish.exe"));
    CHECK(specs.at(0).iconAbs == specs.at(0).targetAbs);
    CHECK(specs.at(0).iconIndex == 0);
    CHECK(specs.at(0).workingDir == QStringLiteral("C:/App"));
    CHECK(specs.at(0).description == QStringLiteral("Dish"));
}

TEST_CASE("installer shortcuts: machine scope propagates into the spec", "[installer][shortcuts]") {
    InstallPlan p;
    p.installDir = QStringLiteral("C:/Program Files/Dish");
    p.scope = Scope::AllUsers;
    InstallState idle;
    idle.phase = InstallPhase::Idle;
    idle.plan = p;

    const auto specs = specsFrom(reduce(idle, ev::Begin{p, manifest()}).effects);
    REQUIRE(specs.size() == 1);
    CHECK(specs.at(0).scope == Scope::AllUsers);
    CHECK(specs.at(0).targetAbs == QStringLiteral("C:/Program Files/Dish/dish.exe"));
}

TEST_CASE("installer shortcuts: create resolves a path, exists follows it, remove clears it",
          "[installer][shortcuts]") {
    FakeShortcutOps shortcuts;
    const ShortcutSpec startMenu = spec(ShortcutLocation::StartMenu);

    CHECK_FALSE(shortcuts.exists(FakeShortcutOps::linkPathFor(startMenu)));

    const auto created = shortcuts.create(startMenu);
    REQUIRE(created.ok);
    // The resolved absolute path comes back through OpResult::path, which is
    // what the installed manifest records in shortcutPaths.
    CHECK(created.path == FakeShortcutOps::linkPathFor(startMenu));
    CHECK(created.path.endsWith(QStringLiteral("/Dish.lnk"))); // one link, no vendor subfolder
    CHECK(shortcuts.exists(created.path));
    REQUIRE(shortcuts.specFor(created.path).has_value());
    CHECK(*shortcuts.specFor(created.path) == startMenu);

    const auto removed = shortcuts.remove(created.path);
    CHECK(removed.ok);
    CHECK_FALSE(shortcuts.exists(created.path));

    // Removing what is already gone is reported, and the journal undo treats
    // that as success.
    const auto again = shortcuts.remove(created.path);
    CHECK_FALSE(again.ok);
    CHECK(again.win32 == dish::test::kFakeErrorFileNotFound);
}

TEST_CASE("installer shortcuts: the four locations resolve to four distinct links",
          "[installer][shortcuts]") {
    FakeShortcutOps shortcuts;
    QStringList paths;
    for (const Scope scope : {Scope::PerUser, Scope::AllUsers}) {
        for (const ShortcutLocation location :
             {ShortcutLocation::StartMenu, ShortcutLocation::Desktop}) {
            const auto result = shortcuts.create(spec(location, scope));
            REQUIRE(result.ok);
            paths.append(result.path);
        }
    }
    CHECK(paths.size() == 4);
    CHECK(QSet<QString>(paths.cbegin(), paths.cend()).size() == 4);
    CHECK(shortcuts.links().size() == 4);
}

TEST_CASE("installer shortcuts: a refused create is typed ShortcutFailed",
          "[installer][shortcuts]") {
    FakeShortcutOps shortcuts;
    shortcuts.failCreates(true);
    const auto result = shortcuts.create(spec(ShortcutLocation::Desktop));
    CHECK_FALSE(result.ok);
    CHECK(result.error == dish::installer::SetupError::ShortcutFailed);
    CHECK(shortcuts.links().isEmpty());
}

TEST_CASE("installer shortcuts: spec equality covers every link field", "[installer][shortcuts]") {
    const ShortcutSpec base = spec(ShortcutLocation::StartMenu);
    CHECK(base == spec(ShortcutLocation::StartMenu));
    CHECK(base != spec(ShortcutLocation::Desktop));
    CHECK(base != spec(ShortcutLocation::StartMenu, Scope::AllUsers));

    ShortcutSpec other = base;
    other.description = QStringLiteral("Dish (beta)");
    CHECK(base != other);
    other = base;
    other.iconIndex = 1;
    CHECK(base != other);
    other = base;
    other.workingDir = QStringLiteral("C:/Other");
    CHECK(base != other);
}
