// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The ARP (Add/Remove Programs) values of spec section 10, value by value, plus
// the EstimatedSize arithmetic and the write/read/delete lifecycle over the
// in-memory registry.
//
// Deliberately NOT exercised here: Win32RegistryOps itself. Its key path is the
// real Uninstall\TinkerNorth.Dish hive, so a test that wrote to it would either
// fabricate an "Installed apps" entry on the developer's machine or clobber a
// genuine Dish install. The 64-bit view (KEY_WOW64_64KEY) and the InstallDate
// stamping live behind that seam and are covered by the CI round-trip
// (scripts/test-installer-roundtrip.ps1), which asserts the real key.

#include "installer/InstallMachine.h"
#include "installer/InstallPlan.h"
#include "installer/ops/RegistryOps.h"

#include "installer/FakeOps.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

using dish::installer::ArpValues;
using dish::installer::InstalledInfo;
using dish::installer::InstallPlan;
using dish::installer::makeArpValues;
using dish::installer::Scope;
using dish::test::FakeRegistryOps;

namespace {

InstallPlan plan(Scope scope = Scope::PerUser,
                 const QString& dir = QStringLiteral("C:/Program Files/Dish")) {
    InstallPlan p;
    p.scope = scope;
    p.installDir = dir;
    return p;
}

} // namespace

TEST_CASE("installer registry: every ARP value of spec section 10", "[installer][registry]") {
    const ArpValues values =
        makeArpValues(plan(), QStringLiteral("1.2.3"), /*totalBytes=*/104857600);

    CHECK(values.displayName == QStringLiteral("Dish"));
    CHECK(values.displayVersion == QStringLiteral("1.2.3"));
    CHECK(values.versionMajor == 1);
    CHECK(values.versionMinor == 2);
    CHECK(values.publisher == QStringLiteral("TinkerNorth"));
    // Native separators: these strings go straight into the shell's UI and into
    // a command line.
    CHECK(values.displayIcon == QStringLiteral("C:\\Program Files\\Dish\\dish.exe,0"));
    CHECK(values.installLocation == QStringLiteral("C:\\Program Files\\Dish"));
    CHECK(values.uninstallString == QStringLiteral("\"C:\\Program Files\\Dish\\uninstall.exe\""));
    CHECK(values.quietUninstallString ==
          QStringLiteral("\"C:\\Program Files\\Dish\\uninstall.exe\" --silent"));
    CHECK(values.urlInfoAbout == QStringLiteral("https://github.com/TinkerNorth/dish-windows"));
    CHECK(values.helpLink == values.urlInfoAbout);
    CHECK(values.installScope == QStringLiteral("user"));
    // Empty means "stamp today's yyyyMMdd at write time": a pure function
    // cannot read the clock.
    CHECK(values.installDate.isEmpty());
    CHECK(values.estimatedSizeKiB == 102400u);
}

TEST_CASE("installer registry: machine scope only changes the scope hint",
          "[installer][registry]") {
    const ArpValues user = makeArpValues(plan(Scope::PerUser), QStringLiteral("1.2.3"), 1024);
    const ArpValues machine = makeArpValues(plan(Scope::AllUsers), QStringLiteral("1.2.3"), 1024);
    CHECK(machine.installScope == QStringLiteral("machine"));
    CHECK(user.installScope == QStringLiteral("user"));

    ArpValues asUser = machine;
    asUser.installScope = QStringLiteral("user");
    CHECK(asUser == user); // the hive, not the values, is what scope decides
}

TEST_CASE("installer registry: EstimatedSize is ceil(bytes / 1024) in KiB",
          "[installer][registry]") {
    const auto kib = [](qint64 bytes) {
        return makeArpValues(plan(), QStringLiteral("1.0.0"), bytes).estimatedSizeKiB;
    };
    CHECK(kib(0) == 0u);
    CHECK(kib(1) == 1u);
    CHECK(kib(1023) == 1u);
    CHECK(kib(1024) == 1u);
    CHECK(kib(1025) == 2u);
    CHECK(kib(2048) == 2u);
    CHECK(kib(1048576) == 1024u);
    // A realistic image: ~100 MB reports six figures of KiB, never zero, which
    // is what the CI round-trip asserts (EstimatedSize > 0).
    CHECK(kib(104857601) > 102400u);
}

TEST_CASE("installer registry: the version triple feeds VersionMajor and VersionMinor",
          "[installer][registry]") {
    const ArpValues zero = makeArpValues(plan(), QStringLiteral("0.1.0"), 0);
    CHECK(zero.versionMajor == 0);
    CHECK(zero.versionMinor == 1);

    const ArpValues big = makeArpValues(plan(), QStringLiteral("12.34.56"), 0);
    CHECK(big.versionMajor == 12);
    CHECK(big.versionMinor == 34);

    // A version that does not parse leaves the DWORDs at zero and still reports
    // the string verbatim: DisplayVersion is what the user sees.
    const ArpValues broken = makeArpValues(plan(), QStringLiteral("not-a-version"), 0);
    CHECK(broken.versionMajor == 0);
    CHECK(broken.versionMinor == 0);
    CHECK(broken.displayVersion == QStringLiteral("not-a-version"));
}

TEST_CASE("installer registry: a trailing slash or native input normalizes the same",
          "[installer][registry]") {
    const ArpValues canonical = makeArpValues(plan(), QStringLiteral("1.0.0"), 0);
    CHECK(makeArpValues(plan(Scope::PerUser, QStringLiteral("C:/Program Files/Dish/")),
                        QStringLiteral("1.0.0"), 0) == canonical);
    CHECK(makeArpValues(plan(Scope::PerUser, QStringLiteral("C:\\Program Files\\Dish")),
                        QStringLiteral("1.0.0"), 0) == canonical);
    CHECK(makeArpValues(plan(Scope::PerUser, QStringLiteral("C:/Program Files/./Dish")),
                        QStringLiteral("1.0.0"), 0) == canonical);
}

TEST_CASE("installer registry: write, read back, delete, per hive", "[installer][registry]") {
    FakeRegistryOps registry;
    const ArpValues values = makeArpValues(plan(), QStringLiteral("1.2.3"), 104857600);

    CHECK_FALSE(registry.readInstalled(Scope::PerUser).has_value());
    CHECK_FALSE(registry.readInstalled(Scope::AllUsers).has_value());

    REQUIRE(registry.writeArp(Scope::PerUser, values).ok);
    const auto info = registry.readInstalled(Scope::PerUser);
    REQUIRE(info.has_value());
    CHECK(info->displayVersion == QStringLiteral("1.2.3"));
    CHECK(info->installLocation == QStringLiteral("C:\\Program Files\\Dish"));
    CHECK(info->installScope == QStringLiteral("user"));
    // The other hive is untouched: the upgrade probe reads both and must be
    // able to tell a per-user install from a machine one.
    CHECK_FALSE(registry.readInstalled(Scope::AllUsers).has_value());

    REQUIRE(registry.deleteArp(Scope::PerUser).ok);
    CHECK_FALSE(registry.readInstalled(Scope::PerUser).has_value());
    CHECK(registry.calls() ==
          QStringList{QStringLiteral("writeArp user"), QStringLiteral("deleteArp user")});
}

TEST_CASE("installer registry: an upgrade overwrites the values in place",
          "[installer][registry]") {
    FakeRegistryOps registry;
    REQUIRE(registry
                .writeArp(Scope::AllUsers,
                          makeArpValues(plan(Scope::AllUsers), QStringLiteral("0.9.0"), 1024))
                .ok);
    REQUIRE(registry
                .writeArp(Scope::AllUsers,
                          makeArpValues(plan(Scope::AllUsers), QStringLiteral("1.0.0"), 2048))
                .ok);
    const auto info = registry.readInstalled(Scope::AllUsers);
    REQUIRE(info.has_value());
    CHECK(info->displayVersion == QStringLiteral("1.0.0"));
    REQUIRE(registry.stored(Scope::AllUsers).has_value());
    CHECK(registry.stored(Scope::AllUsers)->estimatedSizeKiB == 2u);
}

TEST_CASE("installer registry: a refused write is typed RegistryFailed", "[installer][registry]") {
    FakeRegistryOps registry;
    registry.failWrites(true);
    const auto result = registry.writeArp(
        Scope::AllUsers, makeArpValues(plan(Scope::AllUsers), QStringLiteral("1.0.0"), 0));
    CHECK_FALSE(result.ok);
    CHECK(result.error == dish::installer::SetupError::RegistryFailed);
    CHECK_FALSE(registry.stored(Scope::AllUsers).has_value());
}

TEST_CASE("installer registry: InstalledInfo equality is field-wise", "[installer][registry]") {
    InstalledInfo a;
    a.displayVersion = QStringLiteral("1.0.0");
    a.installLocation = QStringLiteral("C:\\App");
    a.installScope = QStringLiteral("user");
    InstalledInfo b = a;
    CHECK(a == b);
    b.installScope = QStringLiteral("machine");
    CHECK(a != b);
}
