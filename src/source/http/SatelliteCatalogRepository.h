// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteCatalogRepository — the per-satellite controller-type catalog cache
// (GET /api/catalog). The catalog is STATIC per server version + locale, so it
// is fetched once and revalidated by ETag: a 200 fills the cache and stores the
// ETag, a conditional request answered 304 serves the cached copy, and a
// transport error / server error / malformed body serves the last good copy
// (stale-on-error) rather than degrading the picker. The "Emulate" picker
// renders from this instead of a hardcoded enum, so a controller type newer than
// the app still gets a name + description from the server.
//
// This is a *caching GATEWAY*, not a durable Repository — it owns IO (it calls
// Wave 1's HTTPClient::getCatalog) and an in-memory ETag cache, so it does NOT
// implement RepositoryContract. Despite the dish-android name
// (`SatelliteCatalogRepository`, which the android architecture doc itself flags
// as "really a caching gateway, not a Repository") it is kept here under
// source/http alongside the TLS verifier seam. The fetch is injected as a
// `Fetch` callable so the cache/revalidate policy is unit-testable with a fake
// transport (no real socket), exactly as android mocks its DiscoveryGateway.

#pragma once

#include "Models/Models.h"

#include <QString>

#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace dish::net {
class HTTPClient;
}

namespace dish::source {

class SatelliteCatalogRepository {
  public:
    // The catalog fetch seam: issue GET /api/catalog against `ip:httpPort` with
    // the given Accept-Language chain and the stored ETag (empty = unconditional
    // GET), and deliver the resulting CatalogDto to `done`. The default binds to
    // HTTPClient::getCatalog; tests inject a fake. The reply carries httpStatus /
    // notModified (304) / reachable / etag, matching getCatalog's callback.
    using Done = std::function<void(const models::CatalogDto&)>;
    using Fetch = std::function<void(const QString& ip, int httpPort, const QString& acceptLanguage,
                                     const QString& etag, Done done)>;

    explicit SatelliteCatalogRepository(Fetch fetch);
    // Convenience overload: binds the fetch to `http->getCatalog`. `http` must
    // outlive this object (the composition root owns both).
    explicit SatelliteCatalogRepository(net::HTTPClient* http);

    using CatalogCb = std::function<void(const std::optional<models::CatalogDto>&)>;

    // Resolve the catalog for `server` keyed by `satelliteId`, applying the
    // revalidate policy, and deliver it to `cb` (nullopt = never been reachable,
    // nothing to serve). `acceptLanguage` is the locale chain (en/es/fr/de/bs/
    // pt-BR, en fallback fine). Async: `cb` fires on the fetch's home thread.
    void catalogFor(const models::DiscoveredServer& server, const QString& satelliteId,
                    const QString& acceptLanguage, CatalogCb cb);

    // The last good cached catalog for `satelliteId`, or nullopt — a synchronous
    // peek that never triggers a fetch (the UI reads it to render immediately).
    std::optional<models::CatalogDto> cached(const QString& satelliteId) const;

  private:
    struct CacheEntry {
        QString etag;
        models::CatalogDto catalog;
    };

    Fetch fetch_;
    mutable std::mutex mutex_;
    std::map<QString, CacheEntry> cache_;
};

} // namespace dish::source
