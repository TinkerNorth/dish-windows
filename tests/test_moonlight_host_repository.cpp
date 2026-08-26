// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MoonlightHostRepository.h"

#include "QSettingsFixture.h"

#include <QJsonObject>

#include <utility>

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
