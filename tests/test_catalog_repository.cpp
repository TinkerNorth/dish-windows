// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Locks the catalog ETag caching gateway: 200 fills the cache + stores the
// ETag, a conditional request answered 304 serves the cached copy, and a
// transport / server / malformed-body failure serves the last good copy
// (stale-on-error) rather than degrading the picker. Replicates dish-android
// repository/SatelliteCatalogRepositoryTest (5 cases) with a fake Fetch (no real
// socket): the fake pops canned CatalogDto replies and records the conditional
// ETag it was handed, exactly as android stubs its DiscoveryGateway.

#include "Models/Models.h"
#include "source/http/SatelliteCatalogRepository.h"

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QString>

#include <deque>
#include <optional>

using dish::models::CatalogDto;
using dish::models::CatalogTypeDto;
using dish::models::DiscoveredServer;
using dish::source::SatelliteCatalogRepository;

namespace {

// Build a CatalogDto in the shape HTTPClient::getCatalog delivers for a given
// HTTP outcome. A "200 fill" carries serverVersion + a controllerType; the cache
// treats an empty serverVersion (a malformed/blank body that fromJson defaulted)
// as not-a-fill.
CatalogDto reply200(const QString& etag, const QString& slug = QStringLiteral("xbox360")) {
    CatalogDto c;
    c.httpStatus = 200;
    c.reachable = true;
    c.notModified = false;
    c.etag = etag;
    c.serverVersion = "1.6.0";
    CatalogTypeDto t;
    t.id = 0;
    t.slug = slug;
    t.name = "Xbox 360 Controller";
    c.controllerTypes.push_back(t);
    return c;
}

CatalogDto reply304() {
    CatalogDto c;
    c.httpStatus = 304;
    c.reachable = true;
    c.notModified = true; // server says "your cache is good"
    return c;
}

CatalogDto replyServerError() {
    CatalogDto c;
    c.httpStatus = 500;
    c.reachable = true;   // a body arrived, but not a 200 catalog
    c.serverVersion = ""; // {"error":"boom"} has no serverVersion
    return c;
}

CatalogDto replyMalformed200() {
    CatalogDto c;
    c.httpStatus = 200;
    c.reachable = true;   // 200, body present
    c.serverVersion = ""; // "not json" → fromJson defaulted → empty serverVersion
    return c;
}

CatalogDto replyUnreachable() {
    CatalogDto c;
    c.httpStatus = 0;
    c.reachable = false; // transport never produced a response
    return c;
}

// A fake Fetch driven by a queue of canned replies; records each conditional
// ETag it is asked with (empty string = unconditional GET).
struct FakeFetch {
    std::deque<CatalogDto> replies;
    QList<QString> receivedEtags;
    bool throwInstead = false; // model a transport that fails outright

    SatelliteCatalogRepository::Fetch fn() {
        return [this](const QString&, int, const QString&, const QString& etag,
                      SatelliteCatalogRepository::Done done) {
            receivedEtags.push_back(etag);
            if (throwInstead || replies.empty()) {
                done(replyUnreachable());
                return;
            }
            CatalogDto next = replies.front();
            replies.pop_front();
            done(next);
        };
    }
};

DiscoveredServer makeServer() {
    DiscoveredServer s;
    s.name = "Pc";
    s.ip = "10.0.0.5";
    s.udpPort = 9876;
    s.machineId = "m1";
    return s;
}

// Synchronous fetch: capture the AsyncState the async cb delivers.
dish::source::CatalogState fetchSync(SatelliteCatalogRepository& repo, const DiscoveredServer& s,
                                     const QString& satId) {
    dish::source::CatalogState captured;
    bool fired = false;
    repo.catalogFor(s, satId, QStringLiteral("en"), [&](const dish::source::CatalogState& r) {
        captured = r;
        fired = true;
    });
    REQUIRE(fired); // the fake fetch is synchronous
    return captured;
}

} // namespace

using dish::source::CatalogError;

TEST_CASE("CatalogCache: a 200 fills the cache and the next fetch revalidates with the stored ETag",
          "[catalog][cache]") {
    FakeFetch fake;
    fake.replies.push_back(reply200(QStringLiteral("\"1.6.0\"")));
    fake.replies.push_back(reply304());
    SatelliteCatalogRepository repo(fake.fn());
    const auto server = makeServer();

    const auto first = fetchSync(repo, server, "sat-1");
    REQUIRE(first.isSuccess());
    REQUIRE_FALSE(first.stale); // a fresh 200
    REQUIRE(first.hasData());
    REQUIRE(first.data->controllerTypes.size() == 1);
    REQUIRE(first.data->controllerTypes.front().slug == "xbox360");
    REQUIRE(fake.receivedEtags[0].isEmpty()); // nothing cached yet → unconditional

    const auto second = fetchSync(repo, server, "sat-1");
    REQUIRE(fake.receivedEtags[1] == "\"1.6.0\""); // If-None-Match from the cache
    REQUIRE(second.isSuccess());
    REQUIRE(second.stale); // 304 → cache re-served, flagged not-freshly-fetched
    REQUIRE(second.data->controllerTypes.front().slug == "xbox360");

    const auto cached = repo.cached("sat-1");
    REQUIRE(cached.has_value());
    REQUIRE(cached->controllerTypes.front().slug == "xbox360");
}

TEST_CASE("CatalogCache: a transport failure serves the last good copy", "[catalog][cache]") {
    FakeFetch fake;
    fake.replies.push_back(reply200(QString())); // first fill, no etag
    SatelliteCatalogRepository repo(fake.fn());
    const auto server = makeServer();

    REQUIRE(fetchSync(repo, server, "sat-1").isSuccess());

    fake.throwInstead = true; // now every fetch fails (unreachable)
    const auto stale = fetchSync(repo, server, "sat-1");
    REQUIRE(stale.isError());                           // failure is no longer dropped
    REQUIRE(*stale.error == CatalogError::Unreachable); // with the typed reason
    REQUIRE(stale.hasData());                           // but the last good copy survives
    REQUIRE(stale.stale);
    REQUIRE(stale.data->controllerTypes.front().slug == "xbox360");
}

TEST_CASE("CatalogCache: a server error keeps the cache, a malformed body too",
          "[catalog][cache]") {
    FakeFetch fake;
    fake.replies.push_back(reply200(QString()));
    fake.replies.push_back(replyServerError());
    fake.replies.push_back(replyMalformed200());
    SatelliteCatalogRepository repo(fake.fn());
    const auto server = makeServer();

    REQUIRE(fetchSync(repo, server, "sat-1").isSuccess());

    const auto afterServerError = fetchSync(repo, server, "sat-1");
    REQUIRE(afterServerError.isError());
    REQUIRE(*afterServerError.error == CatalogError::ServerError);
    REQUIRE(afterServerError.hasData()); // cache survives
    REQUIRE(afterServerError.data->controllerTypes.front().slug == "xbox360");

    const auto afterMalformed = fetchSync(repo, server, "sat-1");
    REQUIRE(afterMalformed.isError());
    REQUIRE(*afterMalformed.error == CatalogError::Malformed);
    REQUIRE(afterMalformed.hasData());
    REQUIRE(afterMalformed.data->controllerTypes.front().slug == "xbox360");
}

TEST_CASE("CatalogCache: a never-reachable satellite yields nullopt, no cache to serve",
          "[catalog][cache]") {
    FakeFetch fake;
    fake.throwInstead = true; // unreachable from the very first call
    SatelliteCatalogRepository repo(fake.fn());
    const auto server = makeServer();

    const auto cold = fetchSync(repo, server, "sat-1");
    REQUIRE(cold.isError());
    REQUIRE(*cold.error == CatalogError::Unreachable);
    REQUIRE_FALSE(cold.hasData()); // never reachable ⇒ no cache to serve
    REQUIRE_FALSE(repo.cached("sat-1").has_value());
}

TEST_CASE("CatalogCache: the cache is keyed per satellite id", "[catalog][cache]") {
    // Windows-specific: two satellites don't share a cache slot (android keys by
    // satelliteId too — version+locale identity is per-satellite here).
    FakeFetch fake;
    fake.replies.push_back(reply200(QStringLiteral("\"1.6.0\""), QStringLiteral("xbox360")));
    fake.replies.push_back(reply200(QStringLiteral("\"1.7.0\""), QStringLiteral("ds4")));
    SatelliteCatalogRepository repo(fake.fn());
    const auto server = makeServer();

    const auto a = fetchSync(repo, server, "sat-A");
    const auto b = fetchSync(repo, server, "sat-B");
    REQUIRE(a.data->controllerTypes.front().slug == "xbox360");
    REQUIRE(b.data->controllerTypes.front().slug == "ds4");
    // sat-A revalidates with ITS etag, not sat-B's.
    REQUIRE(fake.receivedEtags[0].isEmpty());
    REQUIRE(fake.receivedEtags[1].isEmpty());
    REQUIRE(repo.cached("sat-A")->controllerTypes.front().slug == "xbox360");
    REQUIRE(repo.cached("sat-B")->controllerTypes.front().slug == "ds4");
}
