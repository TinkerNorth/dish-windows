// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ConnectionStore — the facade/Coordinator over the three trust+identity repos.
//
// dish-android keeps one ConnectionStore that consolidates a satellite's
// identity across the RememberedSatellite list, the per-id shared-key store, and
// the per-id cert-pin store. This is the C++ port: it owns the three
// repositories (re-exposing them by reference so the TLS seam and the session
// manager read the SAME stores) and concentrates the identity rules:
//
//   * machineId is the stable identity — a box that re-appears at a new IP under
//     the same machineId folds its legacy "ip:port" ghost row and migrates the
//     cert pin + pairing key to the new address (no forced re-pair).
//   * a beacon that arrives WITHOUT a machineId refreshes an existing stable row
//     at the same address in place rather than minting an "ip:port" ghost.
//   * forget() drops the row + its pairing key + its cert pin together.
//
// Coordinator, not a Repository: it issues commands across the three repos and
// re-exposes their state by reference; it never mirrors it. Mirrors dish-android
// repository/ConnectionStore.kt.

#pragma once

#include "Models/Models.h"
#include "repository/RememberedSatelliteRepository.h"
#include "repository/SatellitePinRepository.h"
#include "repository/SatelliteSharedKeyRepository.h"

#include <QList>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

namespace dish::repository {

class ConnectionStore {
  public:
    // Production: pass nullptr to back all three repos on the shared HKCU store.
    // Tests inject one shared in-memory-style QSettings so the namespace-
    // isolation / migration behaviour is observable.
    explicit ConnectionStore(std::shared_ptr<QSettings> settings = nullptr);

    // The stable per-install device id (machineId for X-Device-Id). One owner.
    QString getOrCreateDeviceId();

    // Re-exposed children (never mirrored). The session manager reads/writes the
    // shared-key repo; the TLS verifier reads/writes the pin repo.
    SatellitePinRepository& pins() { return *pins_; }
    SatelliteSharedKeyRepository& sharedKeys() { return *keys_; }
    RememberedSatelliteRepository& satellites() { return *satellites_; }

    QList<models::RememberedWifi> remembered() const;

    // Upsert a satellite seen by discovery / confirmed by a session. Applies the
    // identity-consolidation rules (legacy-ghost collapse, address-change pin
    // migration). Idempotent: an identical row is not re-written.
    void rememberSatellite(const models::DiscoveredServer& server);

    // Re-point ONLY already-remembered rows from a fresh scan. Never adds an
    // un-remembered satellite; a beacon without a machineId never re-points.
    void refreshFromDiscovery(const QList<models::DiscoveredServer>& discovered);

    // Forget a satellite: drops the remembered row, its pairing key, and its
    // cert pin together. Unknown id is a no-op for the other rows/keys/pins.
    void forgetSatellite(const QString& id);

    // Per-id pairing key (the shared key established at pairing).
    std::optional<QString> satelliteSharedKey(const QString& id) const;
    void setSatelliteSharedKey(const QString& id, const QString& keyHex);
    // Drop only the pairing key, leaving the remembered row + cert pin. Used on a
    // terminal 401 / close-notify(unpaired): the satellite stays listed as
    // "Needs pairing" and auto-retry stops.
    void forgetSatelliteSharedKey(const QString& id);

  private:
    // When a beacon with no machineId arrives, refresh an existing STABLE row at
    // the same address in place. Returns true if it handled the server.
    bool refreshKnownBox(const models::DiscoveredServer& server);
    // When a box just gained a stable id, fold every legacy ip:port ghost row at
    // the same address, carrying its pairing key forward.
    void collapseLegacyGhosts(const models::DiscoveredServer& server, const QString& id);
    // The cert pin follows the box to a new address (pin keyed by IP).
    void migratePinOnAddressChange(const std::optional<QString>& oldIp, const QString& newIp);

    std::shared_ptr<QSettings> settings_;
    std::unique_ptr<RememberedSatelliteRepository> satellites_;
    std::unique_ptr<SatelliteSharedKeyRepository> keys_;
    std::unique_ptr<SatellitePinRepository> pins_;
};

} // namespace dish::repository
