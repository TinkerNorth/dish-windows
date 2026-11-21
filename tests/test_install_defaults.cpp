// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/store/InstallDefaults.h"

#include "source/store/ControllerDbPreferenceStore.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/UpdatePreferenceStore.h"

#include <catch2/catch_test_macros.hpp>

#include <QFile>
#include <QIODevice>
#include <QMap>
#include <QRegularExpression>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QXmlStreamReader>

#include <array>
#include <memory>
#include <optional>

using dish::source::applyInstallDefaultsOnce;
using dish::source::ControllerDbPreferenceStore;
using dish::source::CrashReportingStore;
using dish::source::InstallDefaults;
using dish::source::kKeyInstallDefaultsApplied;
using dish::source::kKeyInstallDefaultsControllerUpdates;
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
    ControllerDbPreferenceStore controllerDb{iniSettings(dir, QStringLiteral("controllerdb.ini"))};

    QString appliedStamp() const {
        return appSettings.value(QLatin1String(kKeyInstallDefaultsApplied)).toString();
    }

    std::optional<InstallDefaults> apply() {
        return applyInstallDefaultsOnce(userScope, systemScope, appSettings, updates, crashReports,
                                        controllerDb);
    }

    bool persistedChecksEnabled() const {
        QSettings raw(dir.filePath(QStringLiteral("updates.ini")), QSettings::IniFormat);
        return raw.value(QLatin1String(UpdatePreferenceStore::kKeyChecksEnabled), true).toBool();
    }

    bool persistedCrashReportsEnabled() const {
        QSettings raw(dir.filePath(QStringLiteral("crash.ini")), QSettings::IniFormat);
        return raw.value(QLatin1String(CrashReportingStore::kKeyCollectionEnabled), true).toBool();
    }

    bool persistedControllerUpdatesEnabled() const {
        QSettings raw(dir.filePath(QStringLiteral("controllerdb.ini")), QSettings::IniFormat);
        return raw.value(QLatin1String(ControllerDbPreferenceStore::kKeyChecksEnabled), true)
            .toBool();
    }
};

// What the installer leaves under InstallDefaults: the stamp and the privacy page's three answers.
struct Choices {
    int updateCheck = 1;
    int crashReports = 1;
    int controllerUpdates = 1;
};

void seed(QSettings& scope, const QString& stamp, const Choices& choices) {
    scope.setValue(QLatin1String(kKeyInstallDefaultsStamp), stamp);
    scope.setValue(QLatin1String(kKeyInstallDefaultsUpdateCheck), choices.updateCheck);
    scope.setValue(QLatin1String(kKeyInstallDefaultsCrashReports), choices.crashReports);
    scope.setValue(QLatin1String(kKeyInstallDefaultsControllerUpdates), choices.controllerUpdates);
    scope.sync();
}

constexpr Choices kAllOn{1, 1, 1};
constexpr Choices kAllOff{0, 0, 0};

// The installer's wizard languages, each with the app catalogue it shares its words with.
struct WizardLanguage {
    const char* inno;
    const char* catalogue;
};

constexpr std::array<WizardLanguage, 5> kWizardLanguages{{
    {"english", "en"},
    {"german", "de"},
    {"spanish", "es"},
    {"french", "fr"},
    {"brazilianportuguese", "pt_BR"},
}};

QString installerScript() {
    QFile iss(QStringLiteral(DISH_INSTALLER_ISS_PATH));
    REQUIRE(iss.open(QIODevice::ReadOnly | QIODevice::Text));
    return QString::fromUtf8(iss.readAll());
}

// One [Section]'s entries, without its blank lines and comments.
QStringList sectionLines(const QString& script, const QString& section) {
    const QString header = QLatin1Char('[') + section + QLatin1Char(']');
    const QStringList all = script.split(QLatin1Char('\n'));
    QStringList lines;
    bool inSection = false;
    for (const QString& raw : all) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inSection = line == header;
            continue;
        }
        const bool isEntry = inSection && !line.isEmpty() && !line.startsWith(QLatin1Char(';'));
        if (isEntry) { lines.append(line); }
    }
    return lines;
}

QStringList wizardLanguages(const QString& script) {
    const QRegularExpression name(QStringLiteral("^Name:\\s*\"([^\"]+)\""));
    const QStringList lines = sectionLines(script, QStringLiteral("Languages"));
    QStringList languages;
    for (const QString& line : lines) {
        const QRegularExpressionMatch match = name.match(line);
        if (match.hasMatch()) { languages.append(match.captured(1)); }
    }
    return languages;
}

// One language's [CustomMessages], by message name.
QMap<QString, QString> customMessages(const QString& script, const QString& language) {
    const QString prefix = language + QLatin1Char('.');
    const QStringList lines = sectionLines(script, QStringLiteral("CustomMessages"));
    QMap<QString, QString> messages;
    for (const QString& line : lines) {
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        const bool isOfTheLanguage = line.startsWith(prefix) && equals > prefix.size();
        if (!isOfTheLanguage) { continue; }
        messages.insert(line.mid(prefix.size(), equals - prefix.size()), line.mid(equals + 1));
    }
    return messages;
}

QStringList customMessageNames(const QString& script) {
    QStringList names;
    for (const WizardLanguage& language : kWizardLanguages) {
        names += customMessages(script, QLatin1String(language.inno)).keys();
    }
    names.removeDuplicates();
    return names;
}

// What translations/dish_<catalogue>.ts says for `source`, empty when it has no such message.
QString catalogueTranslation(const QString& catalogue, const QString& source) {
    QFile file(QStringLiteral(DISH_TS_DIR "/dish_%1.ts").arg(catalogue));
    REQUIRE(file.open(QIODevice::ReadOnly));
    QXmlStreamReader xml(&file);
    bool isTheSource = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (!xml.isStartElement()) { continue; }
        if (xml.name() == QLatin1String("source")) {
            isTheSource = xml.readElementText() == source;
        } else if (isTheSource && xml.name() == QLatin1String("translation")) {
            return xml.readElementText();
        }
    }
    return {};
}

} // namespace

TEST_CASE("install defaults: the key names are the installer's registry layout",
          "[install-defaults]") {
    CHECK(QString::fromLatin1(kKeyInstallDefaultsStamp) == QStringLiteral("InstallDefaults/Stamp"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsUpdateCheck) ==
          QStringLiteral("InstallDefaults/UpdateCheck"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsCrashReports) ==
          QStringLiteral("InstallDefaults/CrashReports"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsControllerUpdates) ==
          QStringLiteral("InstallDefaults/ControllerUpdates"));
    CHECK(QString::fromLatin1(kKeyInstallDefaultsApplied) ==
          QStringLiteral("install_defaults_stamp"));

    QFile iss(QStringLiteral(DISH_INSTALLER_ISS_PATH));
    REQUIRE(iss.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString script = QString::fromUtf8(iss.readAll());
    CHECK(script.contains(QStringLiteral("Software\\TinkerNorth\\Dish\\InstallDefaults")));
    CHECK(script.contains(QStringLiteral("ValueName: \"Stamp\"")));
    CHECK(script.contains(QStringLiteral("ValueName: \"UpdateCheck\"")));
    CHECK(script.contains(QStringLiteral("ValueName: \"CrashReports\"")));
    CHECK(script.contains(QStringLiteral("ValueName: \"ControllerUpdates\"")));
    CHECK(script.contains(QStringLiteral("uninsdeletekey")));
    CHECK(script.contains(QStringLiteral("'UPDATECHECK'")));
    CHECK(script.contains(QStringLiteral("'CRASHREPORTS'")));
    CHECK(script.contains(QStringLiteral("'CONTROLLERUPDATES'")));
}

TEST_CASE("install defaults: no stamp in either scope changes nothing", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    f.updates.setChecksEnabled(false);

    SECTION("the key is absent") {}
    SECTION("the key carries an empty stamp") {
        seed(f.userScope, QString(), kAllOff);
        seed(f.systemScope, QString(), kAllOff);
    }

    CHECK_FALSE(readInstallDefaults(f.userScope, f.systemScope).has_value());
    CHECK_FALSE(f.apply().has_value());
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.controllerDb.checksEnabled());
    CHECK(f.appliedStamp().isEmpty());
}

TEST_CASE("install defaults: a new stamp applies every value and records the stamp",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    const QString stamp = QStringLiteral("20261018093000-4711");

    SECTION("off over the on defaults") {
        seed(f.userScope, stamp, kAllOff);

        const auto applied = f.apply();
        REQUIRE(applied.has_value());
        CHECK(applied->stamp == stamp);
        REQUIRE(applied->updateCheck.has_value());
        REQUIRE(applied->crashReports.has_value());
        REQUIRE(applied->controllerUpdates.has_value());
        CHECK_FALSE(*applied->updateCheck);
        CHECK_FALSE(*applied->crashReports);
        CHECK_FALSE(*applied->controllerUpdates);

        CHECK_FALSE(f.updates.checksEnabled());
        CHECK_FALSE(f.crashReports.enabled());
        CHECK_FALSE(f.controllerDb.checksEnabled());
        CHECK_FALSE(f.persistedChecksEnabled());
        CHECK_FALSE(f.persistedCrashReportsEnabled());
        CHECK_FALSE(f.persistedControllerUpdatesEnabled());
    }
    SECTION("on over an earlier off") {
        f.updates.setChecksEnabled(false);
        f.crashReports.setEnabled(false);
        f.controllerDb.setChecksEnabled(false);
        seed(f.userScope, stamp, kAllOn);

        const auto applied = f.apply();
        REQUIRE(applied.has_value());
        CHECK(f.updates.checksEnabled());
        CHECK(f.crashReports.enabled());
        CHECK(f.controllerDb.checksEnabled());
        CHECK(f.persistedChecksEnabled());
        CHECK(f.persistedCrashReportsEnabled());
        CHECK(f.persistedControllerUpdatesEnabled());
    }

    CHECK(f.appliedStamp() == stamp);
    CHECK_FALSE(f.apply().has_value());
}

TEST_CASE("install defaults: the same stamp applies nothing, even when the values changed",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    const QString stamp = QStringLiteral("20261018093000-1");
    seed(f.userScope, stamp, kAllOff);
    REQUIRE(f.apply().has_value());

    f.updates.setChecksEnabled(true);
    f.crashReports.setEnabled(true);
    f.controllerDb.setChecksEnabled(true);
    seed(f.userScope, stamp, kAllOff);

    CHECK_FALSE(f.apply().has_value());
    CHECK(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.controllerDb.checksEnabled());
    CHECK(f.appliedStamp() == stamp);
}

TEST_CASE("install defaults: the user scope beats the system scope", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    seed(f.userScope, QStringLiteral("user-run"), kAllOn);
    seed(f.systemScope, QStringLiteral("system-run"), kAllOff);

    const auto read = readInstallDefaults(f.userScope, f.systemScope);
    REQUIRE(read.has_value());
    CHECK(read->stamp == QStringLiteral("user-run"));
    CHECK(read->controllerUpdates == std::optional<bool>(true));

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    CHECK(applied->stamp == QStringLiteral("user-run"));
    CHECK(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK(f.controllerDb.checksEnabled());
    CHECK(f.appliedStamp() == QStringLiteral("user-run"));
}

TEST_CASE("install defaults: the system scope alone is applied", "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    seed(f.systemScope, QStringLiteral("system-run"), Choices{0, 1, 0});

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    CHECK(applied->stamp == QStringLiteral("system-run"));
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK(f.crashReports.enabled());
    CHECK_FALSE(f.controllerDb.checksEnabled());
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
    CHECK_FALSE(applied->controllerUpdates.has_value());
    CHECK_FALSE(f.updates.checksEnabled());
    CHECK_FALSE(f.crashReports.enabled());
    CHECK(f.controllerDb.checksEnabled());
    CHECK(f.appliedStamp() == QStringLiteral("upgrade-run"));
}

TEST_CASE("install defaults: an upgrade that answers only the controller check applies that one",
          "[install-defaults]") {
    Fixture f;
    REQUIRE(f.dir.isValid());
    f.crashReports.setEnabled(false);
    f.userScope.setValue(QLatin1String(kKeyInstallDefaultsStamp), QStringLiteral("upgrade-run"));
    f.userScope.setValue(QLatin1String(kKeyInstallDefaultsControllerUpdates), 0);
    f.userScope.sync();

    const auto applied = f.apply();
    REQUIRE(applied.has_value());
    CHECK_FALSE(applied->updateCheck.has_value());
    CHECK_FALSE(applied->crashReports.has_value());
    REQUIRE(applied->controllerUpdates.has_value());
    CHECK_FALSE(*applied->controllerUpdates);
    CHECK(f.updates.checksEnabled());
    CHECK_FALSE(f.crashReports.enabled());
    CHECK_FALSE(f.controllerDb.checksEnabled());
    CHECK_FALSE(f.persistedControllerUpdatesEnabled());
    CHECK(f.appliedStamp() == QStringLiteral("upgrade-run"));
}

TEST_CASE("installer: the wizard runs in the five languages its privacy page is written in",
          "[installer]") {
    QStringList expected;
    for (const WizardLanguage& language : kWizardLanguages) {
        expected.append(QLatin1String(language.inno));
    }
    CHECK(wizardLanguages(installerScript()) == expected);
}

TEST_CASE("installer: every privacy message is written in every wizard language", "[installer]") {
    const QString script = installerScript();
    const QStringList names = customMessageNames(script);
    CHECK(names.contains(QStringLiteral("PrivacyControllerUpdates")));
    for (const WizardLanguage& language : kWizardLanguages) {
        const QMap<QString, QString> messages =
            customMessages(script, QLatin1String(language.inno));
        for (const QString& name : names) {
            INFO(language.inno << "." << name.toStdString());
            CHECK_FALSE(messages.value(name).trimmed().isEmpty());
        }
    }
}

TEST_CASE("installer: the privacy page names every host the app contacts on its own",
          "[installer]") {
    const QString script = installerScript();
    const QString github = QStringLiteral("GitHub");
    const QString sentry = QStringLiteral("Sentry");
    const QString website = QStringLiteral("dish.tinkernorth.com");
    for (const WizardLanguage& language : kWizardLanguages) {
        INFO(language.inno);
        const QMap<QString, QString> messages =
            customMessages(script, QLatin1String(language.inno));
        const QString description = messages.value(QStringLiteral("PrivacyDescription"));
        CHECK(description.contains(github));
        CHECK(description.contains(sentry));
        CHECK(description.contains(website));
        CHECK(messages.value(QStringLiteral("PrivacyUpdateCheck")).contains(github));
        CHECK(messages.value(QStringLiteral("PrivacyCrashReports")).contains(sentry));
        CHECK(messages.value(QStringLiteral("PrivacyControllerUpdates")).contains(website));
    }
}

TEST_CASE("installer: the upgrade note names each switch as Settings words it", "[installer]") {
    const QString script = installerScript();
    const std::array<QString, 3> switches{
        QStringLiteral("Check for updates automatically"), QStringLiteral("Share crash reports"),
        QStringLiteral("Check for controller updates automatically")};
    for (const WizardLanguage& language : kWizardLanguages) {
        const QString kept = customMessages(script, QLatin1String(language.inno))
                                 .value(QStringLiteral("PrivacyKept"));
        for (const QString& label : switches) {
            const QString words = catalogueTranslation(QLatin1String(language.catalogue), label);
            INFO(language.inno << ": " << words.toStdString());
            REQUIRE_FALSE(words.isEmpty());
            CHECK(kept.contains(words));
        }
    }
}
