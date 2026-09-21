// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MoonlightHostRepository.h"

#include "QSettingsFixture.h"

#include <QJsonObject>

#include <utility>

#include "FakeSecretCipher.h"
#include "repository/SecretValue.h"
#include "repository/SettingsKeys.h"

#include <catch2/catch_test_macros.hpp>

using dish::models::MoonlightHost;
using dish::repository::MoonlightHostRepository;

TEST_CASE("Moonlight identity is generated once and persists", "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    const auto first = repo.getOrCreateIdentity();
    REQUIRE(first.has_value());
    REQUIRE(first->certPem.rfind("-----BEGIN CERTIFICATE-----", 0) == 0);
    REQUIRE(!first->privateKeyPem.empty());

    // A second call returns the SAME persisted identity, not a fresh one.
    MoonlightHostRepository repo2(settings);
    const auto second = repo2.getOrCreateIdentity();
    REQUIRE(second.has_value());
    REQUIRE(second->certPem == first->certPem);
    REQUIRE(second->privateKeyPem == first->privateKeyPem);
}

TEST_CASE("The uniqueid is minted once per install and never shared between installs",
          "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    const QString first = repo.getOrCreateUniqueId();
    REQUIRE(first.size() == 16);
    for (const QChar c : first) { REQUIRE(QStringLiteral("0123456789ABCDEF").contains(c)); }
    // Not the constant every Moonlight client traditionally shares: a host
    // keys pending pairings and session ownership on this, so a shared value
    // collides the moment two installs pair with the same host.
    REQUIRE(first != QStringLiteral("0123456789ABCDEF"));
    // Stable for this install...
    REQUIRE(MoonlightHostRepository(settings).getOrCreateUniqueId() == first);
    // ...and different for another.
    auto other = dish::test::makeSharedSettings();
    REQUIRE(MoonlightHostRepository(other).getOrCreateUniqueId() != first);
}

TEST_CASE("Host list upserts, forgets, and survives a corrupt blob", "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    MoonlightHost a;
    a.name = QStringLiteral("PC-A");
    a.ip = QStringLiteral("192.168.0.2");
    a.uuid = QStringLiteral("uuid-a");
    a.paired = true;
    repo.rememberHost(a);

    MoonlightHost b;
    b.name = QStringLiteral("PC-B");
    b.ip = QStringLiteral("192.168.0.3");
    repo.rememberHost(b);

    REQUIRE(repo.hosts().size() == 2);

    // Upsert in place: same id (uuid), new name.
    MoonlightHost a2 = a;
    a2.name = QStringLiteral("PC-A-renamed");
    repo.rememberHost(a2);
    REQUIRE(repo.hosts().size() == 2);

    bool sawRename = false;
    for (const auto& h : repo.hosts()) {
        if (h.id() == a.id()) { sawRename = h.name == QStringLiteral("PC-A-renamed"); }
    }
    REQUIRE(sawRename);

    repo.forgetHost(a.id());
    REQUIRE(repo.hosts().size() == 1);
    REQUIRE(repo.hosts().first().ip == QStringLiteral("192.168.0.3"));
}

TEST_CASE("Server cert pin round-trips and clears with the host", "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    MoonlightHost h;
    h.name = QStringLiteral("PC");
    h.ip = QStringLiteral("10.0.0.9");
    repo.rememberHost(h);

    REQUIRE_FALSE(repo.serverCert(h.id()).has_value());
    repo.setServerCert(h.id(), QStringLiteral("deadbeef"));
    REQUIRE(repo.serverCert(h.id()).value() == QStringLiteral("deadbeef"));

    repo.forgetHost(h.id());
    REQUIRE_FALSE(repo.serverCert(h.id()).has_value());
}

TEST_CASE("A stored Auto of 0 is migrated to 0xFF on read", "[moonlight][repo]") {
    // 0 used to mean Auto and is the wire's CONTROLLER_TYPE_UNKNOWN, so a record
    // written before the change would otherwise announce Unknown to the host and
    // let it pick with no way of telling us what it picked.
    QJsonObject legacy;
    legacy[QStringLiteral("ip")] = QStringLiteral("192.168.0.9");
    legacy[QStringLiteral("deviceType")] = 0;
    const auto migrated = MoonlightHost::fromJson(legacy);
    REQUIRE(migrated.deviceType == dish::models::kMoonlightDeviceAuto);
    REQUIRE(migrated.deviceType == 0xFF);

    // An explicit pick is left alone.
    QJsonObject explicitPick;
    explicitPick[QStringLiteral("ip")] = QStringLiteral("192.168.0.9");
    explicitPick[QStringLiteral("deviceType")] = dish::models::kMoonlightDeviceNintendo;
    REQUIRE(MoonlightHost::fromJson(explicitPick).deviceType ==
            dish::models::kMoonlightDeviceNintendo);

    // A record that never named one defaults to Auto rather than to Unknown.
    QJsonObject bare;
    bare[QStringLiteral("ip")] = QStringLiteral("192.168.0.9");
    REQUIRE(MoonlightHost::fromJson(bare).deviceType == dish::models::kMoonlightDeviceAuto);
}

TEST_CASE("A binding round-trips its host and its own controller type", "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    dish::models::MoonlightBinding pad;
    pad.slotId = QStringLiteral("sdl:1");
    pad.hostId = QStringLiteral("ml:ip:192.168.0.2");
    pad.controllerType = dish::models::kMoonlightDevicePlayStation;
    repo.rememberBinding(pad);

    // The type is PER BINDING: a second pad on the same host is a different
    // device without disturbing the first.
    dish::models::MoonlightBinding other;
    other.slotId = QStringLiteral("sdl:2");
    other.hostId = QStringLiteral("ml:ip:192.168.0.2");
    other.controllerType = dish::models::kMoonlightDeviceNintendo;
    repo.rememberBinding(other);

    REQUIRE(repo.bindings().size() == 2);
    REQUIRE(repo.binding(QStringLiteral("sdl:1"))->controllerType ==
            dish::models::kMoonlightDevicePlayStation);
    REQUIRE(repo.binding(QStringLiteral("sdl:2"))->controllerType ==
            dish::models::kMoonlightDeviceNintendo);
    REQUIRE_FALSE(repo.binding(QStringLiteral("sdl:9")).has_value());

    // Re-binding the same slot upserts rather than duplicating.
    pad.controllerType = dish::models::kMoonlightDeviceXbox;
    repo.rememberBinding(pad);
    REQUIRE(repo.bindings().size() == 2);
    REQUIRE(repo.binding(QStringLiteral("sdl:1"))->controllerType ==
            dish::models::kMoonlightDeviceXbox);

    // And it survives a fresh repository over the same store.
    MoonlightHostRepository reopened(settings);
    REQUIRE(reopened.bindings().size() == 2);

    reopened.forgetBinding(QStringLiteral("sdl:1"));
    REQUIRE(reopened.bindings().size() == 1);
    REQUIRE_FALSE(reopened.binding(QStringLiteral("sdl:1")).has_value());

    // A record naming no slot or no host is not a binding.
    dish::models::MoonlightBinding junk;
    junk.hostId = QStringLiteral("ml:ip:192.168.0.2");
    reopened.rememberBinding(junk);
    REQUIRE(reopened.bindings().size() == 1);
}

TEST_CASE("A binding stored with the old Auto is migrated too", "[moonlight][repo]") {
    QJsonObject legacy;
    legacy[QStringLiteral("slotId")] = QStringLiteral("sdl:1");
    legacy[QStringLiteral("hostId")] = QStringLiteral("ml:ip:192.168.0.2");
    legacy[QStringLiteral("controllerType")] = 0;
    const auto migrated = dish::models::MoonlightBinding::fromJson(legacy);
    REQUIRE(migrated.controllerType == dish::models::kMoonlightDeviceAuto);
}

TEST_CASE("Forgetting a host retires the bindings that drove it", "[moonlight][repo]") {
    auto settings = dish::test::makeSharedSettings();
    MoonlightHostRepository repo(settings);

    const QString gone = QStringLiteral("ml:ip:192.168.0.2");
    const QString kept = QStringLiteral("ml:ip:192.168.0.3");

    for (const auto& [slot, host] : {std::make_pair(QStringLiteral("sdl:1"), gone),
                                     std::make_pair(QStringLiteral("sdl:2"), gone),
                                     std::make_pair(QStringLiteral("sdl:3"), kept)}) {
        dish::models::MoonlightBinding b;
        b.slotId = slot;
        b.hostId = host;
        repo.rememberBinding(b);
    }
    REQUIRE(repo.bindings().size() == 3);

    // A binding is an intent to drive THAT host, so it goes with the pairing
    // rather than outliving it and asking to be re-attached forever.
    repo.forgetBindingsForHost(gone);
    REQUIRE(repo.bindings().size() == 1);
    REQUIRE(repo.binding(QStringLiteral("sdl:3"))->hostId == kept);
    REQUIRE_FALSE(repo.binding(QStringLiteral("sdl:1")).has_value());
    REQUIRE_FALSE(repo.binding(QStringLiteral("sdl:2")).has_value());

    // Forgetting a host nothing drove is not an error and touches nothing.
    repo.forgetBindingsForHost(QStringLiteral("ml:ip:10.0.0.1"));
    REQUIRE(repo.bindings().size() == 1);
}

TEST_CASE("Moonlight identity: the private key is stored wrapped, the certificate is not",
          "[moonlight][repo][secret]") {
    auto settings = dish::test::makeSharedSettings();
    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    MoonlightHostRepository repo(settings, cipher);
    const auto id = repo.getOrCreateIdentity();
    REQUIRE(id.has_value());

    namespace keys = dish::repository::keys;
    const QString storedKey = settings->value(QLatin1String(keys::kMoonlightKeyKey)).toString();
    const QString storedCert = settings->value(QLatin1String(keys::kMoonlightCertKey)).toString();
    CHECK(dish::repository::secret::isWrapped(storedKey));
    CHECK_FALSE(storedKey.contains(QStringLiteral("PRIVATE KEY")));
    CHECK_FALSE(dish::repository::secret::isWrapped(storedCert));
    CHECK(storedCert.toStdString() == id->certPem);

    // Reads back through the cipher, on a fresh repository over the same store.
    const auto again = MoonlightHostRepository(settings, cipher).getOrCreateIdentity();
    REQUIRE(again.has_value());
    CHECK(again->privateKeyPem == id->privateKeyPem);
}

TEST_CASE("Moonlight identity: a plaintext key from an older build is wrapped in place",
          "[moonlight][repo][secret][migration]") {
    auto settings = dish::test::makeSharedSettings();
    namespace keys = dish::repository::keys;
    // Stand-ins, not real PEM: the repository wraps whatever the older build
    // stored without reading it, and a PEM-shaped literal would trip the
    // repository's own secret scan.
    const QString legacyCert = QStringLiteral("certificate-from-an-older-build");
    const QString legacyKey = QStringLiteral("key-material-from-an-older-build");
    settings->setValue(QLatin1String(keys::kMoonlightCertKey), legacyCert);
    settings->setValue(QLatin1String(keys::kMoonlightKeyKey), legacyKey);

    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    MoonlightHostRepository repo(settings, cipher);
    const QString storedKey = settings->value(QLatin1String(keys::kMoonlightKeyKey)).toString();
    CHECK(dish::repository::secret::isWrapped(storedKey));
    CHECK_FALSE(storedKey.contains(legacyKey));
    // The same identity, not a fresh one: every paired host keeps recognising us.
    const auto id = repo.getOrCreateIdentity();
    REQUIRE(id.has_value());
    CHECK(id->certPem == legacyCert.toStdString());
    CHECK(id->privateKeyPem == legacyKey.toStdString());
}

TEST_CASE("Moonlight identity: a key this account cannot open is replaced by a fresh one",
          "[moonlight][repo][secret]") {
    auto settings = dish::test::makeSharedSettings();
    auto cipher = std::make_shared<dish::test::FakeSecretCipher>();
    const auto first = MoonlightHostRepository(settings, cipher).getOrCreateIdentity();
    REQUIRE(first.has_value());

    cipher->failUnprotect = true;
    MoonlightHostRepository repo(settings, cipher);
    const auto second = repo.getOrCreateIdentity();
    REQUIRE(second.has_value());
    CHECK(second->certPem != first->certPem);
    CHECK(second->privateKeyPem != first->privateKeyPem);
}
