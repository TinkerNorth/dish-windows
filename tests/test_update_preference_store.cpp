// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The three reactive update preferences and the four imperative bookkeeping
// keys. The key NAMES are pinned as literals on purpose: they are a persisted
// schema in HKCU\Software\TinkerNorth\Dish that the boot gate (which runs
// before QGuiApplication sets the org names) reaches by naming the same hive
// explicitly. Renaming one silently resets a user's choice, so a rename has to
// break this test.
//
// Every case runs against a temp INI through the injecting constructor: no test
// here touches the real registry.

#include "source/store/UpdatePreferenceStore.h"

#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>
#include <QTemporaryDir>

#include <memory>

using dish::source::kKeyUpdatesHandoffAttempts;
using dish::source::kKeyUpdatesHandoffVersion;
using dish::source::kKeyUpdatesLastCheckUtcMs;
using dish::source::kKeyUpdatesLastRunVersion;
using dish::source::UpdatePreferences;
using dish::source::UpdatePreferenceStore;
using dish::test::StateSourceProbe;

namespace {

std::unique_ptr<UpdatePreferenceStore> makeStore(const QString& iniPath) {
    return std::make_unique<UpdatePreferenceStore>(
        std::make_unique<QSettings>(iniPath, QSettings::IniFormat));
}

} // namespace

TEST_CASE("update preference store: the key names are a persisted schema",
          "[update][update-prefs]") {
    CHECK(QString::fromLatin1(UpdatePreferenceStore::kKeyChecksEnabled) ==
          QStringLiteral("updates_check_enabled"));
    CHECK(QString::fromLatin1(UpdatePreferenceStore::kKeyAutoDownload) ==
          QStringLiteral("updates_auto_download"));
    CHECK(QString::fromLatin1(UpdatePreferenceStore::kKeySkippedVersion) ==
          QStringLiteral("updates_skipped_version"));
    // The imperative half: written by the boot gate and the coordinator, never
    // part of the reactive slice.
    CHECK(QString::fromLatin1(kKeyUpdatesLastCheckUtcMs) ==
          QStringLiteral("updates_last_check_utc_ms"));
    CHECK(QString::fromLatin1(kKeyUpdatesHandoffVersion) ==
          QStringLiteral("updates_handoff_version"));
    CHECK(QString::fromLatin1(kKeyUpdatesHandoffAttempts) ==
          QStringLiteral("updates_handoff_attempts"));
    CHECK(QString::fromLatin1(kKeyUpdatesLastRunVersion) ==
          QStringLiteral("updates_last_run_version"));
}

TEST_CASE("update preference store: defaults are on, on and nothing skipped",
          "[update][update-prefs]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const auto store = makeStore(temp.filePath(QStringLiteral("fresh.ini")));

    CHECK(store->checksEnabled());
    CHECK(store->autoDownload());
    CHECK(store->skippedVersion().isEmpty());

    const UpdatePreferences initial = store->state().value();
    CHECK(initial == UpdatePreferences{});
}

TEST_CASE("update preference store: every setter persists under its own key",
          "[update][update-prefs]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString ini = temp.filePath(QStringLiteral("prefs.ini"));

    {
        const auto store = makeStore(ini);
        store->setChecksEnabled(false);
        store->setAutoDownload(false);
        store->setSkippedVersion(QStringLiteral("0.3.0"));
    }

    // The raw file, so a renamed key cannot hide behind the store's own reader.
    QSettings raw(ini, QSettings::IniFormat);
    CHECK(raw.value(QLatin1String(UpdatePreferenceStore::kKeyChecksEnabled)).toBool() == false);
    CHECK(raw.value(QLatin1String(UpdatePreferenceStore::kKeyAutoDownload)).toBool() == false);
    CHECK(raw.value(QLatin1String(UpdatePreferenceStore::kKeySkippedVersion)).toString() ==
          QStringLiteral("0.3.0"));

    // And a fresh store over the same file reads them back.
    const auto reopened = makeStore(ini);
    CHECK_FALSE(reopened->checksEnabled());
    CHECK_FALSE(reopened->autoDownload());
    CHECK(reopened->skippedVersion() == QStringLiteral("0.3.0"));
}

TEST_CASE("update preference store: an explicit true is read back as true",
          "[update][update-prefs]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString ini = temp.filePath(QStringLiteral("explicit.ini"));
    {
        QSettings seed(ini, QSettings::IniFormat);
        seed.setValue(QLatin1String(UpdatePreferenceStore::kKeyChecksEnabled), true);
        seed.setValue(QLatin1String(UpdatePreferenceStore::kKeyAutoDownload), false);
        seed.setValue(QLatin1String(UpdatePreferenceStore::kKeySkippedVersion),
                      QStringLiteral("1.0.0"));
        seed.sync();
    }
    const auto store = makeStore(ini);
    CHECK(store->checksEnabled());
    CHECK_FALSE(store->autoDownload());
    CHECK(store->skippedVersion() == QStringLiteral("1.0.0"));
}

TEST_CASE("update preference store: the slice republishes once per real change",
          "[update][update-prefs]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const auto store = makeStore(temp.filePath(QStringLiteral("emit.ini")));

    StateSourceProbe<UpdatePreferences> probe(store->state());
    REQUIRE(probe.count() == 1); // the initial value
    CHECK(probe.latest() == UpdatePreferences{});

    store->setAutoDownload(false);
    CHECK(probe.count() == 2);
    CHECK_FALSE(probe.latest().autoDownload);

    // Idempotent: a repeat set neither persists nor re-emits, so the
    // coordinator cannot loop through its own subscription.
    store->setAutoDownload(false);
    CHECK(probe.count() == 2);

    store->setChecksEnabled(false);
    CHECK(probe.count() == 3);
    CHECK_FALSE(probe.latest().checksEnabled);
    store->setChecksEnabled(false);
    CHECK(probe.count() == 3);

    store->setSkippedVersion(QStringLiteral("0.4.0"));
    CHECK(probe.count() == 4);
    CHECK(probe.latest().skippedVersion == QStringLiteral("0.4.0"));
    store->setSkippedVersion(QStringLiteral("0.4.0"));
    CHECK(probe.count() == 4);

    // Clearing the mute is a change like any other.
    store->setSkippedVersion(QString());
    CHECK(probe.count() == 5);
    CHECK(probe.latest().skippedVersion.isEmpty());
}

TEST_CASE("update preference store: the preference slice compares field-wise",
          "[update][update-prefs]") {
    UpdatePreferences a;
    UpdatePreferences b;
    CHECK(a == b);
    b.checksEnabled = false;
    CHECK(a != b);
    b = a;
    b.autoDownload = false;
    CHECK(a != b);
    b = a;
    b.skippedVersion = QStringLiteral("0.2.0");
    CHECK(a != b);
}
