// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/SatelliteSharedKeyRepository.h"

#include "FakeSecretCipher.h"
#include "QSettingsFixture.h"
#include "RepositoryContract.h"
#include "repository/SecretValue.h"
#include "repository/SettingsKeys.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <algorithm>

using dish::repository::SatelliteSharedKeyRepository;
using dish::test::makeSharedSettings;
namespace keys = dish::repository::keys;

namespace {
bool contains(const std::vector<QString>& v, const QString& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}
} // namespace

TEST_CASE("SatelliteSharedKeyRepository satisfies the repository contract", "[repository][key]") {
    dish::test::runRepositoryContract<QString, QString>(
        [] { return std::make_unique<SatelliteSharedKeyRepository>(makeSharedSettings()); },
        [](int i) { return QStringLiteral("satellite:mid:%1").arg(i); },
        [](const QString& k) { return QStringLiteral("key-") + k; });
}

TEST_CASE("put then get round-trips by id", "[key]") {
    SatelliteSharedKeyRepository keysRepo(makeSharedSettings());
    keysRepo.put("satellite:mid:abc", "DEADBEEF");
    CHECK(keysRepo.get("satellite:mid:abc") == QStringLiteral("DEADBEEF"));
}

TEST_CASE("an unknown id has no key", "[key]") {
    SatelliteSharedKeyRepository keysRepo(makeSharedSettings());
    CHECK_FALSE(keysRepo.get("satellite:mid:nope").has_value());
}

TEST_CASE("keys survive into a fresh repo over the same store", "[key]") {
    auto store = makeSharedSettings();
    SatelliteSharedKeyRepository(store).put("satellite:mid:abc", "KEY1");
    CHECK(SatelliteSharedKeyRepository(store).get("satellite:mid:abc") == QStringLiteral("KEY1"));
}

TEST_CASE("remove drops one key and leaves the others", "[key]") {
    SatelliteSharedKeyRepository keysRepo(makeSharedSettings());
    keysRepo.put("satellite:mid:a", "A");
    keysRepo.put("satellite:mid:b", "B");
    keysRepo.remove("satellite:mid:a");
    CHECK_FALSE(keysRepo.get("satellite:mid:a").has_value());
    CHECK(keysRepo.get("satellite:mid:b") == QStringLiteral("B"));
}

TEST_CASE("all returns only shared-key values, ignoring co-tenant entries", "[key]") {
    auto store = makeSharedSettings();
    // Co-tenant entries that must NOT appear in all().
    store->setValue(QLatin1String(keys::kSatelliteListKey), "[]");
    store->setValue(QLatin1String(keys::kCertPinPrefix) + QStringLiteral("x"), "pin-value");
    SatelliteSharedKeyRepository keysRepo(store);
    keysRepo.put("satellite:mid:a", "A");
    keysRepo.put("satellite:mid:b", "B");
    const auto all = keysRepo.all();
    CHECK(all.size() == 2);
    CHECK(contains(all, QStringLiteral("A")));
    CHECK(contains(all, QStringLiteral("B")));
    CHECK_FALSE(contains(all, QStringLiteral("pin-value")));
}

TEST_CASE("clear removes every shared key but preserves co-tenant prefs", "[key]") {
    auto store = makeSharedSettings();
    store->setValue(QLatin1String(keys::kSatelliteListKey), "preserved");
    SatelliteSharedKeyRepository keysRepo(store);
    keysRepo.put("satellite:mid:a", "A");
    keysRepo.put("satellite:mid:b", "B");
    keysRepo.clear();
    CHECK(keysRepo.all().empty());
    CHECK(store->value(QLatin1String(keys::kSatelliteListKey)).toString() ==
          QStringLiteral("preserved"));
}

// ── At rest: the value in the store is never the key ─────────────────────────

TEST_CASE("a stored key is wrapped by the cipher, and reads back through it", "[key][secret]") {
    auto store = makeSharedSettings();
    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    SatelliteSharedKeyRepository keysRepo(store, cipher);
    keysRepo.put("satellite:mid:abc", "DEADBEEF");

    const QString stored =
        store->value(QLatin1String(keys::kSharedKeyPrefix) + QStringLiteral("satellite:mid:abc"))
            .toString();
    CHECK(dish::repository::secret::isWrapped(stored));
    CHECK_FALSE(stored.contains(QStringLiteral("DEADBEEF")));
    CHECK(cipher->protects == 1);

    CHECK(keysRepo.get("satellite:mid:abc") == QStringLiteral("DEADBEEF"));
    CHECK(cipher->unprotects >= 1);
}

TEST_CASE("a plaintext key from an older build is wrapped in place on construction",
          "[key][secret][migration]") {
    auto store = makeSharedSettings();
    const QString settingsKey =
        QLatin1String(keys::kSharedKeyPrefix) + QStringLiteral("satellite:mid:old");
    store->setValue(settingsKey, QStringLiteral("CAFEBABE"));

    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    SatelliteSharedKeyRepository keysRepo(store, cipher);
    const QString stored = store->value(settingsKey).toString();
    CHECK(dish::repository::secret::isWrapped(stored));
    CHECK_FALSE(stored.contains(QStringLiteral("CAFEBABE")));
    // Nobody re-pairs for this: the key still reads back.
    CHECK(keysRepo.get("satellite:mid:old") == QStringLiteral("CAFEBABE"));
    // A second construction over the same store finds nothing left to wrap.
    SatelliteSharedKeyRepository again(store, cipher);
    CHECK(cipher->protects == 1);
    CHECK(again.get("satellite:mid:old") == QStringLiteral("CAFEBABE"));
}

TEST_CASE("a wrapped key the platform will not open reads as absent, not as garbage",
          "[key][secret]") {
    auto store = makeSharedSettings();
    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    SatelliteSharedKeyRepository keysRepo(store, cipher);
    keysRepo.put("satellite:mid:a", "AAAA");
    keysRepo.put("satellite:mid:b", "BBBB");

    cipher->failUnprotect = true;
    CHECK_FALSE(keysRepo.get("satellite:mid:a").has_value());
    CHECK(keysRepo.all().empty());
    // The value is left where it is: a re-pair overwrites it, nothing deletes it.
    CHECK_FALSE(
        store->value(QLatin1String(keys::kSharedKeyPrefix) + QStringLiteral("satellite:mid:a"))
            .toString()
            .isEmpty());
}

TEST_CASE("a protect the platform refuses stores the key unwrapped rather than losing it",
          "[key][secret]") {
    auto store = makeSharedSettings();
    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    cipher->failProtect = true;
    SatelliteSharedKeyRepository keysRepo(store, cipher);
    keysRepo.put("satellite:mid:a", "AAAA");
    const QString stored =
        store->value(QLatin1String(keys::kSharedKeyPrefix) + QStringLiteral("satellite:mid:a"))
            .toString();
    CHECK_FALSE(dish::repository::secret::isWrapped(stored));
    CHECK(keysRepo.get("satellite:mid:a") == QStringLiteral("AAAA"));
}

TEST_CASE("the real cipher is the default: a key round-trips under DPAPI", "[key][secret][dpapi]") {
    auto store = makeSharedSettings();
    SatelliteSharedKeyRepository keysRepo(store);
    keysRepo.put("satellite:mid:abc", "0123456789abcdef");
    const QString stored =
        store->value(QLatin1String(keys::kSharedKeyPrefix) + QStringLiteral("satellite:mid:abc"))
            .toString();
    CHECK(dish::repository::secret::isWrapped(stored));
    CHECK_FALSE(stored.contains(QStringLiteral("0123456789abcdef")));
    CHECK(SatelliteSharedKeyRepository(store).get("satellite:mid:abc") ==
          QStringLiteral("0123456789abcdef"));
}
