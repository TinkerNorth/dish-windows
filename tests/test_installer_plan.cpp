// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The destination deny-list and the plan's defaults. Everything here is a
// typed status rather than a bool, because the UI phrases each case
// differently and silently "fixing" a bad path is how an installer ends up
// writing into %WINDIR%.
//
// The filesystem cases run inside a QTemporaryDir; the deny-list cases are
// decided before any filesystem access, which is also why the UNC-root case is
// safe to assert without a server.

#include "installer/InstallPlan.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using dish::installer::ClosePolicy;
using dish::installer::DirStatus;
using dish::installer::installedManifestFileName;
using dish::installer::InstallPlan;
using dish::installer::oldDirName;
using dish::installer::Scope;
using dish::installer::scopeFromToken;
using dish::installer::scopeToken;
using dish::installer::stageDirName;
using dish::installer::validateInstallDir;

namespace {

QString envPath(const char* name) { return QDir::fromNativeSeparators(qEnvironmentVariable(name)); }

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return file.write(bytes) == bytes.size();
}

} // namespace

TEST_CASE("installer plan: defaults are per-user, Start Menu on, nothing else",
          "[installer][plan]") {
    const InstallPlan p;
    CHECK(p.scope == Scope::PerUser);
    CHECK(p.installDir.isEmpty());
    CHECK(p.startMenu);
    CHECK_FALSE(p.desktop);
    CHECK_FALSE(p.launch);
    CHECK_FALSE(p.allowDowngrade);
    CHECK(p.closePolicy == ClosePolicy::Abort);
    CHECK_FALSE(p.isUpgrade);
    CHECK(p.existingVersion.isEmpty());
    CHECK(p.existingDir.isEmpty());
}

TEST_CASE("installer plan: equality covers every field", "[installer][plan]") {
    const InstallPlan base;
    InstallPlan other = base;
    CHECK(base == other);

    other.desktop = true;
    CHECK(base != other);

    // The upgrade lock: a plan pinned to an existing install is a different
    // plan, so a relaunch can never silently drop the recorded destination.
    InstallPlan upgrade = base;
    upgrade.isUpgrade = true;
    upgrade.existingVersion = QStringLiteral("0.9.0");
    upgrade.existingDir = QStringLiteral("C:/Old");
    CHECK(base != upgrade);
    InstallPlan sameUpgrade = upgrade;
    CHECK(upgrade == sameUpgrade);
    sameUpgrade.existingDir = QStringLiteral("C:/Other");
    CHECK(upgrade != sameUpgrade);
}

TEST_CASE("installer plan: scope tokens are the one registry and manifest spelling",
          "[installer][plan]") {
    CHECK(scopeToken(Scope::PerUser) == QStringLiteral("user"));
    CHECK(scopeToken(Scope::AllUsers) == QStringLiteral("machine"));
    CHECK(scopeFromToken(QStringLiteral("user")) == Scope::PerUser);
    CHECK(scopeFromToken(QStringLiteral("machine")) == Scope::AllUsers);
    CHECK(scopeFromToken(QStringLiteral("MACHINE")) == Scope::AllUsers); // CLI is case-insensitive
    CHECK_FALSE(scopeFromToken(QStringLiteral("all-users")).has_value());
    CHECK_FALSE(scopeFromToken(QString()).has_value());
}

TEST_CASE("installer plan: the working-directory names are format, not preference",
          "[installer][plan]") {
    CHECK(installedManifestFileName() == QStringLiteral(".dish-manifest.json"));
    CHECK(stageDirName() == QStringLiteral(".dish-stage"));
    CHECK(oldDirName() == QStringLiteral(".dish-old"));
}

TEST_CASE("installer plan: relative and malformed destinations are rejected", "[installer][plan]") {
    CHECK(validateInstallDir(QString()) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("   ")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("Dish")) == DirStatus::NotAbsolute);
    CHECK(validateInstallDir(QStringLiteral("./Dish")) == DirStatus::NotAbsolute);
    CHECK(validateInstallDir(QStringLiteral("\\Dish")) == DirStatus::NotAbsolute);
    // "C:foo" is drive-RELATIVE, which resolves against a per-drive cwd.
    CHECK(validateInstallDir(QStringLiteral("C:Dish")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("C:/Di<sh")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("C:/Di|sh")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("C:/Di\"sh")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("C:/Dish?")) == DirStatus::Invalid);
    CHECK(validateInstallDir(QStringLiteral("C:/Dish*")) == DirStatus::Invalid);
    // A second colon is never a drive separator.
    CHECK(validateInstallDir(QStringLiteral("C:/Di:sh")) == DirStatus::Invalid);
}

TEST_CASE("installer plan: roots are denied", "[installer][plan]") {
    CHECK(validateInstallDir(QStringLiteral("C:/")) == DirStatus::Denied);
    CHECK(validateInstallDir(QStringLiteral("C:\\")) == DirStatus::Denied);
    CHECK(validateInstallDir(QStringLiteral("D:/")) == DirStatus::Denied);
    // A UNC share root, decided before any network access.
    CHECK(validateInstallDir(QStringLiteral("//server/share")) == DirStatus::Denied);
    CHECK(validateInstallDir(QStringLiteral("\\\\server\\share")) == DirStatus::Denied);
}

TEST_CASE("installer plan: the Windows directory and the Program Files roots are denied",
          "[installer][plan]") {
    const QString windir = envPath("WINDIR");
    REQUIRE_FALSE(windir.isEmpty());
    CHECK(validateInstallDir(windir) == DirStatus::IsSystem);
    CHECK(validateInstallDir(windir + QStringLiteral("/System32")) == DirStatus::IsSystem);
    CHECK(validateInstallDir(windir + QStringLiteral("/Dish")) == DirStatus::IsSystem);

    const QString programFiles = envPath("ProgramFiles");
    REQUIRE_FALSE(programFiles.isEmpty());
    // The root itself is denied; a subdirectory of it is the DEFAULT all-users
    // destination, so it must stay acceptable.
    CHECK(validateInstallDir(programFiles) == DirStatus::Denied);
    CHECK(validateInstallDir(programFiles + QStringLiteral("/DishNoSuchDir-8f2a1c")) ==
          DirStatus::Ok);

    const QString profile = envPath("USERPROFILE");
    REQUIRE_FALSE(profile.isEmpty());
    CHECK(validateInstallDir(profile) == DirStatus::Denied);
    CHECK(validateInstallDir(profile + QStringLiteral("/DishNoSuchDir-8f2a1c")) == DirStatus::Ok);
}

TEST_CASE("installer plan: existing directories are classified by what is in them",
          "[installer][plan]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());

    SECTION("a nonexistent destination is fine: the install creates it") {
        CHECK(validateInstallDir(root + QStringLiteral("/Fresh/Dish")) == DirStatus::Ok);
    }
    SECTION("an empty directory is fine") {
        REQUIRE(QDir().mkpath(root + QStringLiteral("/Empty")));
        CHECK(validateInstallDir(root + QStringLiteral("/Empty")) == DirStatus::Ok);
    }
    SECTION("a directory with foreign content is refused") {
        REQUIRE(QDir().mkpath(root + QStringLiteral("/Busy")));
        REQUIRE(writeFile(root + QStringLiteral("/Busy/notes.txt"), "x"));
        CHECK(validateInstallDir(root + QStringLiteral("/Busy")) == DirStatus::NotEmpty);
    }
    SECTION("a recorded install is an upgrade target, not a refusal") {
        const QString dir = root + QStringLiteral("/Installed");
        REQUIRE(QDir().mkpath(dir));
        REQUIRE(writeFile(dir + QStringLiteral("/dish.exe"), "MZ"));
        REQUIRE(writeFile(dir + QLatin1Char('/') + installedManifestFileName(), "{}"));
        CHECK(validateInstallDir(dir) == DirStatus::IsExistingInstall);
    }
    SECTION("a file is not a directory") {
        const QString file = root + QStringLiteral("/plain.txt");
        REQUIRE(writeFile(file, "x"));
        CHECK(validateInstallDir(file) == DirStatus::Invalid);
    }
    SECTION("native separators and trailing slashes normalize") {
        REQUIRE(QDir().mkpath(root + QStringLiteral("/Empty2")));
        const QString native = QDir::toNativeSeparators(root + QStringLiteral("/Empty2"));
        CHECK(validateInstallDir(native) == DirStatus::Ok);
        CHECK(validateInstallDir(root + QStringLiteral("/Empty2/")) == DirStatus::Ok);
        CHECK(validateInstallDir(root + QStringLiteral("/Empty2/../Empty2")) == DirStatus::Ok);
    }
}
