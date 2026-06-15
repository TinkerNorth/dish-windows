// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/http/SatelliteCatalogRepository.h"

#include "Network/HTTPClient.h"

#include <utility>

namespace dish::source {

namespace {

// A reply is a valid catalog to cache iff the server answered 200 with a body
// that parsed into a real catalog. The contract guarantees a 200 catalog carries
// a non-empty `serverVersion`, so an empty one is the tell for a malformed /
// blank body (HTTPClient::getCatalog already ran fromJson on it; a garbage body
// yields a default-constructed DTO). A non-200 (server error) or unreachable
// reply is likewise not a fill. Mirrors android's `status != 200 || body blank`
// + decode-failure arms collapsed into one predicate.
bool isValidFill(const models::CatalogDto& reply) {
    return reply.httpStatus == 200 && reply.reachable && !reply.serverVersion.isEmpty();
}

} // namespace

SatelliteCatalogRepository::SatelliteCatalogRepository(Fetch fetch) : fetch_(std::move(fetch)) {}

SatelliteCatalogRepository::SatelliteCatalogRepository(net::HTTPClient* http)
    : fetch_([http](const QString& ip, int httpPort, const QString& acceptLanguage,
                    const QString& etag, Done done) {
          http->getCatalog(ip, httpPort, acceptLanguage, etag, std::move(done));
      }) {}

void SatelliteCatalogRepository::catalogFor(const models::DiscoveredServer& server,
                                            const QString& satelliteId,
                                            const QString& acceptLanguage, CatalogCb cb) {
    // Snapshot the cached entry (etag + last good copy) under the lock.
    QString conditionalEtag;
    std::optional<models::CatalogDto> cachedCatalog;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = cache_.find(satelliteId);
        if (it != cache_.end()) {
            conditionalEtag = it->second.etag;
            cachedCatalog = it->second.catalog;
        }
    }

    fetch_(server.ip, server.httpPort, acceptLanguage, conditionalEtag,
           [this, satelliteId, cachedCatalog, cb = std::move(cb)](const models::CatalogDto& reply) {
               // 304: the cache is still valid — serve it (nullopt if, somehow,
               // we 304'd with nothing cached).
               if (reply.notModified) {
                   cb(cachedCatalog);
                   return;
               }
               // Not a fresh, well-formed 200 (server error, unreachable, or a
               // malformed body): stale-on-error — serve the last good copy.
               if (!isValidFill(reply)) {
                   cb(cachedCatalog);
                   return;
               }
               // A good 200: fill the cache (store the response ETag for the
               // next revalidation) and serve the fresh catalog.
               {
                   std::lock_guard<std::mutex> lock(mutex_);
                   cache_[satelliteId] = CacheEntry{reply.etag, reply};
               }
               cb(reply);
           });
}

std::optional<models::CatalogDto>
SatelliteCatalogRepository::cached(const QString& satelliteId) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = cache_.find(satelliteId);
    if (it == cache_.end()) { return std::nullopt; }
    return it->second.catalog;
}

} // namespace dish::source
