// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// QML migration (A-ext) — the AppViewModel settings/about/deadzone/license
// EXPOSURE layer. The AppViewModel itself is a thin forwarder over the already-
// tested stores (it adds no behaviour), so what is worth pinning is the MAPPING
// it re-projects: the theme-mode int<->enum contract (the QML chip speaks 0/1/2),
// the deadzone row folding (durable override + default seed + per-device motion
// toggle), and the license-row projection (the same display rules the Widgets
// view uses). These pure helpers (qml/AppSettingsMaps) are exercised directly
// against the real stores over isolated in-memory QSettings — the same seam the
// sibling store tests use — so no heavyweight AppModel / Qt Quick is needed.
//
// The store round-trips (themeMode get/set, crash toggle, onboarding gate +
// markWelcomeCompleted) are checked THROUGH the int maps so the forwarding the
// AppViewModel does (setThemeMode -> themeModeFromInt -> store->setMode, etc.) is
// covered end-to-end at the value level.

#include "qml/AppSettingsMaps.h"

#include "repository/DeadzoneRepository.h"
#include "repository/MotionPreferenceRepository.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/MotionEnabledStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ThemePreferenceStore.h"
#include "UI/licenses/LicenseManifest.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QSettings>
#include <QString>
#include <QUuid>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>

#include <memory>

using dish::qml::deadzoneRowFor;
using dish::qml::kDefaultDeadzoneStickFlat;
using dish::qml::kDefaultDeadzoneTriggerFlat;
using dish::qml::licenseRows;
using dish::qml::themeModeFromInt;
using dish::qml::themeModeToInt;
using dish::repository::DeadzoneRepository;
using dish::repository::MotionPreferenceRepository;
using dish::source::CrashReportingStore;
using dish::source::MotionEnabledStore;
using dish::source::OnboardingPreferenceStore;
using dish::source::ThemeMode;
using dish::source::ThemePreferenceStore;
using dish::test::makeSharedSettings;

namespace {

// A fresh, unique, file-backed QSettings (never the real HKCU registry) — the
// scalar-preference stores take ownership of a std::unique_ptr<QSettings>.
std::unique_ptr<QSettings> uniqueIniSettings(const char* tag) {
    const QString path = QDir::tempPath() + QStringLiteral("/dish-%1-").arg(tag) +
                         QUuid::createUuid().toString(QUuid::WithoutBraces) +
                         QStringLiteral(".ini");
    return std::make_unique<QSettings>(path, QSettings::IniFormat);
}

} // namespace

// ── Theme-mode int <-> enum (the QML segmented control's 0/1/2 contract) ─────

TEST_CASE("themeMode int contract is Light=0 Dark=1 System=2", "[appvm][theme]") {
    REQUIRE(themeModeToInt(ThemeMode::Light) == 0);
    REQUIRE(themeModeToInt(ThemeMode::Dark) == 1);
    REQUIRE(themeModeToInt(ThemeMode::System) == 2);
    REQUIRE(themeModeFromInt(0) == ThemeMode::Light);
    REQUIRE(themeModeFromInt(1) == ThemeMode::Dark);
    REQUIRE(themeModeFromInt(2) == ThemeMode::System);
}

TEST_CASE("themeMode int round-trips for all three modes", "[appvm][theme]") {
    for (auto m : {ThemeMode::Light, ThemeMode::Dark, ThemeMode::System}) {
        REQUIRE(themeModeFromInt(themeModeToInt(m)) == m);
    }
}

TEST_CASE("themeModeFromInt is lenient -- out-of-range falls back to System", "[appvm][theme]") {
    REQUIRE(themeModeFromInt(-1) == ThemeMode::System);
    REQUIRE(themeModeFromInt(3) == ThemeMode::System);
    REQUIRE(themeModeFromInt(99) == ThemeMode::System);
}

TEST_CASE("themeMode get/set round-trips through the store via the int maps", "[appvm][theme]") {
    // The exact path AppViewModel::setThemeMode/themeMode take: int -> enum ->
    // store->setMode, and store->mode -> enum -> int.
    ThemePreferenceStore store(uniqueIniSettings("theme"));
    REQUIRE(themeModeToInt(store.mode()) == 2); // fresh store defaults to System

    store.setMode(themeModeFromInt(1)); // "Dark" from the chip
    REQUIRE(store.mode() == ThemeMode::Dark);
    REQUIRE(themeModeToInt(store.mode()) == 1);

    store.setMode(themeModeFromInt(0)); // "Light"
    REQUIRE(themeModeToInt(store.mode()) == 0);
}

// ── Crash-reporting toggle forwarding ────────────────────────────────────────

TEST_CASE("crash-reporting toggle forwards through the store", "[appvm][crash]") {
    CrashReportingStore store(uniqueIniSettings("crash"));
    REQUIRE(store.enabled() == true); // opt-out default-ON (D4)

    store.setEnabled(false); // the AppViewModel setter forwards verbatim
    REQUIRE(store.enabled() == false);
    store.setEnabled(true);
    REQUIRE(store.enabled() == true);
}

// ── Deadzone row mapping + set forwarding ────────────────────────────────────

TEST_CASE("deadzoneRowFor seeds the default profile for an unset device", "[appvm][deadzone]") {
    DeadzoneRepository repo(makeSharedSettings());
    MotionPreferenceRepository motionRepo(makeSharedSettings());
    MotionEnabledStore motion(&motionRepo);

    const auto row = deadzoneRowFor(QStringLiteral("pad-1"), QStringLiteral("DualSense"),
                                    /*hasGyro=*/true, &repo, &motion);
    CHECK(row.value(QStringLiteral("id")).toString() == QStringLiteral("pad-1"));
    CHECK(row.value(QStringLiteral("name")).toString() == QStringLiteral("DualSense"));
    CHECK(row.value(QStringLiteral("hasGyro")).toBool() == true);
    CHECK(row.value(QStringLiteral("stickFlat")).toInt() == kDefaultDeadzoneStickFlat);
    CHECK(row.value(QStringLiteral("triggerFlat")).toInt() == kDefaultDeadzoneTriggerFlat);
    // Motion defaults ON for an untouched device.
    CHECK(row.value(QStringLiteral("forwardMotion")).toBool() == true);
}

TEST_CASE("deadzoneRowFor reflects a stored override + a disabled motion toggle",
          "[appvm][deadzone]") {
    DeadzoneRepository repo(makeSharedSettings());
    MotionPreferenceRepository motionRepo(makeSharedSettings());
    MotionEnabledStore motion(&motionRepo);

    // Drive the exact mutations AppViewModel::setDeadzones / setMotionEnabled do.
    repo.setDeadzones(QStringLiteral("pad-1"), {5000, 40});
    motion.setEnabled(std::string("pad-1"), false);

    const auto row = deadzoneRowFor(QStringLiteral("pad-1"), QStringLiteral("Pad"),
                                    /*hasGyro=*/true, &repo, &motion);
    CHECK(row.value(QStringLiteral("stickFlat")).toInt() == 5000);
    CHECK(row.value(QStringLiteral("triggerFlat")).toInt() == 40);
    CHECK(row.value(QStringLiteral("forwardMotion")).toBool() == false);
}

TEST_CASE("deadzone/motion are keyed by device id and stay independent", "[appvm][deadzone]") {
    DeadzoneRepository repo(makeSharedSettings());
    MotionPreferenceRepository motionRepo(makeSharedSettings());
    MotionEnabledStore motion(&motionRepo);

    repo.setDeadzones(QStringLiteral("a"), {1111, 11});
    motion.setEnabled(std::string("b"), false);

    const auto a = deadzoneRowFor(QStringLiteral("a"), QString(), true, &repo, &motion);
    const auto b = deadzoneRowFor(QStringLiteral("b"), QString(), true, &repo, &motion);

    // a carries its override but a default-ON motion; b carries the default
    // deadzone but the explicit motion-off.
    CHECK(a.value(QStringLiteral("stickFlat")).toInt() == 1111);
    CHECK(a.value(QStringLiteral("forwardMotion")).toBool() == true);
    CHECK(b.value(QStringLiteral("stickFlat")).toInt() == kDefaultDeadzoneStickFlat);
    CHECK(b.value(QStringLiteral("forwardMotion")).toBool() == false);
}

TEST_CASE("deadzoneRowFor tolerates null stores (pure defaults)", "[appvm][deadzone]") {
    const auto row =
        deadzoneRowFor(QStringLiteral("x"), QStringLiteral("X"), false, nullptr, nullptr);
    CHECK(row.value(QStringLiteral("stickFlat")).toInt() == kDefaultDeadzoneStickFlat);
    CHECK(row.value(QStringLiteral("triggerFlat")).toInt() == kDefaultDeadzoneTriggerFlat);
    CHECK(row.value(QStringLiteral("forwardMotion")).toBool() ==
          MotionEnabledStore::kDefaultEnabled);
}

// ── License rows ─────────────────────────────────────────────────────────────

TEST_CASE("licenseRows maps a manifest to {name,version,license,url}", "[appvm][licenses]") {
    const auto manifest = dish::ui::parseLicenseManifest(QByteArray(R"({
        "libraries":[
          {"name":"Qt 6","version":"6.7.3","url":"https://qt.io",
           "licenses":[{"name":"LGPL-3.0","url":"https://l/lgpl"}]},
          {"group":"io.lib","artifact":"thing","version":"1.0"}
        ]
    })"));

    const QVariantList rows = licenseRows(manifest);
    REQUIRE(rows.size() == 2);

    const auto first = rows.at(0).toMap();
    CHECK(first.value(QStringLiteral("name")).toString() == QStringLiteral("Qt 6"));
    CHECK(first.value(QStringLiteral("version")).toString() == QStringLiteral("6.7.3"));
    CHECK(first.value(QStringLiteral("license")).toString() == QStringLiteral("LGPL-3.0"));
    // Click-url precedence: licenses[0].url wins over the entry url.
    CHECK(first.value(QStringLiteral("url")).toString() == QStringLiteral("https://l/lgpl"));

    // The second entry has no name -> falls back to group:artifact; no license
    // block -> an empty license label, and no url -> empty.
    const auto second = rows.at(1).toMap();
    CHECK(second.value(QStringLiteral("name")).toString() == QStringLiteral("io.lib:thing"));
    CHECK(second.value(QStringLiteral("license")).toString().isEmpty());
    CHECK(second.value(QStringLiteral("url")).toString().isEmpty());
}

TEST_CASE("licenseRows drops an unnamed entry", "[appvm][licenses]") {
    // No name, no group, no artifact -> displayName empty -> the row is hidden,
    // mirroring the adapter rule.
    const auto manifest = dish::ui::parseLicenseManifest(QByteArray(R"({
        "libraries":[ {"version":"9"} ]
    })"));
    CHECK(licenseRows(manifest).isEmpty());
}

// ── Onboarding gate + markWelcomeCompleted forwarding ────────────────────────

TEST_CASE("onboardingNeeded reflects the store and clears on markWelcomeCompleted",
          "[appvm][onboarding]") {
    OnboardingPreferenceStore store(uniqueIniSettings("onb"));
    // onboardingNeeded == !welcomeCompleted(); a fresh store needs onboarding.
    REQUIRE(store.welcomeCompleted() == false);

    store.markWelcomeCompleted(); // the AppViewModel command forwards verbatim
    REQUIRE(store.welcomeCompleted() == true);

    // Idempotent: a second mark stays completed.
    store.markWelcomeCompleted();
    REQUIRE(store.welcomeCompleted() == true);
}
