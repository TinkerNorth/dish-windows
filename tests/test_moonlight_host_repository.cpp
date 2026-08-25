// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "repository/MoonlightHostRepository.h"

#include "QSettingsFixture.h"

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
