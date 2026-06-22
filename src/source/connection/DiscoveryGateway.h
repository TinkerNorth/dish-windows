// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// DiscoveryGateway — fuses the two discovery paths into one source-tagged list.
//
// A Gateway (IO + pure merge, no domain state): it runs the broadcast beacon
// (LANDiscovery) and the mDNS browse (MdnsDiscovery) and merges their results,
// de-duplicating by the STABLE key (machineId-preferring) so one physical
// receiver heard on both paths collapses to a single row tagged Both — not two
// rows keyed by address. Mirrors dish-android core/net/DiscoveryGateway's
// mergeDiscovered + pinId.
//
// The machineId-keyed dedupe is the fix over the old address-keyed merge: a box
// that answers the beacon on one port and mDNS with a different SRV port, but
// advertises the same machineId, is one satellite.

#pragma once

#include "Models/Models.h"

#include <QList>
#include <QString>

namespace dish::net {

class DiscoveryGateway {
  public:
    // Merge broadcast + mDNS results. De-dupes by DiscoveredServer stable key
    // (machineId else ip:udpPort); broadcast-only → Broadcast, mdns-only → Mdns,
    // both → Both. Result is sorted by name. Pure (static).
    static QList<models::DiscoveredServer>
    mergeDiscovered(const QList<models::DiscoveredServer>& broadcast,
                    const QList<models::DiscoveredServer>& mdns);

    // The id a cert pin is filed under for a discovered satellite: the explicit
    // satellite id when present, else the observed ip. Mirrors android's
    // DiscoveryGateway.pinId. Pure (static).
    static QString pinId(const QString& satelliteId, const QString& ip);

    // Run both discovery paths and return the merged, source-tagged list.
    // Blocking; call from a background thread.
    static QList<models::DiscoveredServer> discover();
};

} // namespace dish::net
