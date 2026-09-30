// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The builds before 0.0.1 kept pairing keys in plain hex under
// "wifi_shared_key/<id>". The store carries each one to its current name, where
// the key repository wraps it with DPAPI, and a copy left under the retired name
// would be the same secret in plaintext beside it. Each case runs the real store
// (and so the real DPAPI) over the temp INI the other store tests use.

#include "repository/ConnectionStore.h"
#include "repository/SettingsKeys.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QSettings>
#include <QString>

#include <memory>

using dish::repository::ConnectionStore;
using dish::test::makeSharedSettings;
namespace keys = dish::repository::keys;

namespace {

const QString kId = QStringLiteral("mid:m1");
const QString kKeyAA = QString(64, QLatin1Char('a'));
const QString kKeyBB = QString(64, QLatin1Char('b'));

QString retiredName(const QString& id) { return QLatin1String(keys::kLegacySharedKeyPrefix) + id; }

QString currentName(const QString& id) { return QLatin1String(keys::kSharedKeyPrefix) + id; }

// Whether any value in the store still holds `secret` as written, under any name.
bool holdsInPlaintext(QSettings& settings, const QString& secret) {
    for (const auto& name : settings.allKeys()) {
        if (settings.value(name).toString().contains(secret)) { return true; }
    }
    return false;
}

// The next launch: a new settings object over the same file, and a new store over it.
std::unique_ptr<ConnectionStore> relaunch(QSettings& settings) {
    settings.sync();
    auto reopened = std::make_shared<QSettings>(settings.fileName(), QSettings::IniFormat);
    return std::make_unique<ConnectionStore>(std::move(reopened));
}

} // namespace

TEST_CASE("a retired pairing key reads back under its current name after the upgrade",
          "[cstore][migration]") {
    const auto settings = makeSharedSettings();
    settings->setValue(retiredName(kId), kKeyAA);

    const ConnectionStore store(settings);

    CHECK(store.satelliteSharedKey(kId) == kKeyAA);
}

TEST_CASE("the upgrade leaves no plaintext pairing key under the retired name",
          "[cstore][migration]") {
    const auto settings = makeSharedSettings();
    settings->setValue(retiredName(kId), kKeyAA);

    const ConnectionStore store(settings);

    CHECK_FALSE(settings->contains(retiredName(kId)));
    CHECK_FALSE(holdsInPlaintext(*settings, kKeyAA));
}

TEST_CASE("a retired key beside a current one is dropped, and the current one is kept",
          "[cstore][migration]") {
    const auto settings = makeSharedSettings();
    settings->setValue(retiredName(kId), kKeyBB);
    settings->setValue(currentName(kId), kKeyAA);

    const ConnectionStore store(settings);

    CHECK(store.satelliteSharedKey(kId) == kKeyAA);
    CHECK_FALSE(settings->contains(retiredName(kId)));
    CHECK_FALSE(holdsInPlaintext(*settings, kKeyBB));
}

TEST_CASE("a forgotten satellite's key does not come back from the retired name at the next launch",
          "[cstore][migration]") {
    const auto settings = makeSharedSettings();
    settings->setValue(retiredName(kId), kKeyAA);
    ConnectionStore(settings).forgetSatellite(kId);

    const auto nextLaunch = relaunch(*settings);

    CHECK_FALSE(nextLaunch->satelliteSharedKey(kId).has_value());
}
