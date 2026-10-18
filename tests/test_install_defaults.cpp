// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/store/InstallDefaults.h"

#include "source/store/CrashReportingStore.h"
#include "source/store/UpdatePreferenceStore.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QIODevice>
#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>
#include <optional>

using dish::source::applyInstallDefaultsOnce;
using dish::source::CrashReportingStore;
using dish::source::InstallDefaults;
using dish::source::kKeyInstallDefaultsApplied;
using dish::source::kKeyInstallDefaultsCrashReports;
using dish::source::kKeyInstallDefaultsStamp;
using dish::source::kKeyInstallDefaultsUpdateCheck;
using dish::source::readInstallDefaults;
using dish::source::UpdatePreferenceStore;

namespace {

std::unique_ptr<QSettings> iniSettings(const QTemporaryDir& dir, const QString& name) {
    return std::make_unique<QSettings>(dir.filePath(name), QSettings::IniFormat);
}

struct Fixture {
    QTemporaryDir dir;
    QSettings userScope{dir.filePath(QStringLiteral("user.ini")), QSettings::IniFormat};
    QSettings systemScope{dir.filePath(QStringLiteral("system.ini")), QSettings::IniFormat};
    QSettings appSettings{dir.filePath(QStringLiteral("app.ini")), QSettings::IniFormat};
    UpdatePreferenceStore updates{iniSettings(dir, QStringLiteral("updates.ini"))};
    CrashReportingStore crashReports{iniSettings(dir, QStringLiteral("crash.ini"))};

    QString appliedStamp() const {
        return appSettings.value(QLatin1String(kKeyInstallDefaultsApplied)).toString();
    }

    std::optional<InstallDefaults> apply() {
        return applyInstallDefaultsOnce(userScope, systemScope, appSettings, updates,
                                        crashReports);
    }

    bool persistedChecksEnabled() const {
        QSettings raw(dir.filePath(QStringLiteral("updates.ini")), QSettings::IniFormat);
        return raw.value(QLatin1String(UpdatePreferenceStore::kKeyChecksEnabled), true).toBool();
    }

    bool persistedCrashReportsEnabled() const {
        QSettings raw(dir.filePath(QStringLiteral("crash.ini")), QSettings::IniFormat);
        return raw.value(QLatin1String(CrashReportingStore::kKeyCollectionEnabled), true).toBool();
    }
};

void seed(QSettings& scope, const QString& stamp, int updateCheck, int crashReports) {
    scope.setValue(QLatin1String(kKeyInstallDefaultsStamp), stamp);
    scope.setValue(QLatin1String(kKeyInstallDefaultsUpdateCheck), updateCheck);
    scope.setValue(QLatin1String(kKeyInstallDefaultsCrashReports), crashReports);
    scope.sync();
}

} // namespace

TEST_CASE("install defaults: the key names are the installer's registry layout",
          "[install-defaults]") {
    CHECK(QString::fromLatin1(kKeyInstallDefaultsStamp) == QStringLiteral("InstallDefaults/Stamp"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsUpdateCheck) ==
          QStringLiteral("InstallDefaults/UpdateCheck"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsCrashReports) ==
          QStringLiteral("InstallDefaults/CrashReports"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsApplied) ==
          QStringLiteral("install_defaults_stamp"));

    QFile iss(QStringLiteral(DISH_INSTALLER_ISS_PATH));
    REQUIRE(iss.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString script = QString::fromUtf8(iss.readAll());
    CHECK(script.contains(QStringLiteral("Software\\TinkerNorth\\Dish\\InstallDefaults")));
    CHECK(script.contains(QStringLiteral("ValueName: \"Stamp\"")));
    CHECK(script.contains(QStringLiteral("ValueName: \"UpdateCheck\"")));
    CHECK(script.contains(QStringLiteral("ValueName: \"CrashReports\"")));
    CHECK(script.contains(QStringLiteral("uninsdeletekey")));
    CHECK(script.contains(QStringLiteral("'UPDATECHECK'")));
    CHECK(script.contains(QStringLiteral("'CRASHREPORTS'")));
}

TEST_CASE("install defaults: no stamp in either scope changes nothing", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    f.updates.setChecksEnabled(false);

    SECTION("the key is absent") {}
    SECTION("the key carries an empty stamp") {
        seed(f.userScope, QString(), 1, 1);
        seed(f.systemScope, QString(), 1, 1);
    }

    CHECK_FALSE(readInstallDefaults(f.userScope, f.systemScope).has_value());
    CHECK_FALSE(f.apply().has_value());
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.appliedStamp().isEmpty());
}

TEST_CASE("install defaults: a new stamp applies both values and records the stamp",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    const QString stamp = QStringLiteral("20261018093000-4711");

    SECTION("off over the on defaults") {
        seed(f.userScope, stamp, 0, 0);

        const auto applied = f.apply();
        REQUIRE(applied.has_value());
        CHECK(applied->stamp == stamp);
        REQUIRE(applied->updateCheck.has_value());
        REQUIRE(applied->crashReports.has_value());
        CHECK_FALSE(*applied->updateCheck);
        CHECK_FALSE(*applied->crashReports);

        CHECK_FALSE(f.updates.checksEnabled());
        CHECK_FALSE(f.crashReports.enabled());
        CHECK_FALSE(f.persistedChecksEnabled());
        CHECK_FALSE(f.persistedCrashReportsEnabled());
    }
    SECTION("on over an earlier off") {
        f.updates.setChecksEnabled(false);
        f.crashReports.setEnabled(false);
        seed(f.userScope, stamp, 1, 1);

        const auto applied = f.apply();
        REQUIRE(applied.has_value());
        CHECK(f.updates.checksEnabled());
        CHECK(f.crashReports.enabled());
        CHECK(f.persistedChecksEnabled());
        CHECK(f.persistedCrashReportsEnabled());
    }

    CHECK(f.appliedStamp() == stamp);
    CHECK_FALSE(f.apply().has_value());
}

TEST_CASE("install defaults: the same stamp applies nothing, even when the values changed",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    const QString stamp = QStringLiteral("20261018093000-1");
    seed(f.userScope, stamp, 0, 0);
    REQUIRE(f.apply().has_value());

    f.updates.setChecksEnabled(true);
    f.crashReports.setEnabled(true);
    seed(f.userScope, stamp, 0, 0);

    CHECK_FALSE(f.apply().has_value());
    CHECK(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.appliedStamp() == stamp);
}

TEST_CASE("install defaults: the user scope beats the system scope", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    seed(f.userScope, QStringLiteral("user-run"), 1, 1);
    seed(f.systemScope, QStringLiteral("system-run"), 0, 0);

    const auto read = readInstallDefaults(f.userScope, f.systemScope);
    REQUIRE(read.has_value());
    CHECK(read->stamp == QStringLiteral("user-run"));

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    CHECK(applied->stamp == QStringLiteral("user-run"));
    CHECK(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.appliedStamp() == QStringLiteral("user-run"));
}

TEST_CASE("install defaults: the system scope alone is applied", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    seed(f.systemScope, QStringLiteral("system-run"), 0, 1);

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    CHECK(applied->stamp == QStringLiteral("system-run"));
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.appliedStamp() == QStringLiteral("system-run"));
}

TEST_CASE("install defaults: a value the installer did not write is left alone",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    f.crashReports.setEnabled(false);
    f.userScope.setValue(QLatin1String(kKeyInstallDefaultsStamp), QStringLiteral("upgrade-run"));
    f.userScope.setValue(QLatin1String(kKeyInstallDefaultsUpdateCheck), 0);
    f.userScope.sync();

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    REQUIRE(applied->updateCheck.has_value());
    CHECK_FALSE(*applied->updateCheck);
    CHECK_FALSE(applied->crashReports.has_value());
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK_FALSE(f.crashReports.enabled());
    CHECK(f.appliedStamp() == QStringLiteral("upgrade-run"));
}
