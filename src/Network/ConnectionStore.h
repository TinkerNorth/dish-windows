// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"
#include "repository/ConnectionStore.h"

#include <QList>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

namespace dish::net {

// Thin compatibility adapter over the repository-layer ConnectionStore facade
// (dish::repository::ConnectionStore). Wave 2a carved the monolithic store into
// three repositories + a Coordinator; this adapter keeps the pre-2a call surface
// (remember / forget / sharedKey / setSharedKey / forgetKey / remembered /
// getOrCreateDeviceId) so the existing consumers — WifiConnectionManager,
// ConnectionHub, AppModel — compile unchanged while transparently gaining the
// machineId-consolidation, legacy-upgrade, and pin/key-migration behaviour.
//
// The facade is the source of truth; new code should depend on it directly and
// reach the cert-pin store through facade().pins(). This adapter exists only to
// avoid a wide mechanical refactor of the Wave-1 session layer in this slice.
class ConnectionStore {
  public:
    explicit ConnectionStore(std::unique_ptr<QSettings> settings = nullptr);

    QString getOrCreateDeviceId() { return facade_->getOrCreateDeviceId(); }

    QList<models::RememberedWifi> remembered() const { return facade_->remembered(); }
    void remember(const models::DiscoveredServer& server) { facade_->rememberSatellite(server); }
    // Re-point ONLY already-remembered rows from a fresh scan (durable). The
    // session manager calls this on every discovery completion so a satellite
    // that moved to a new DHCP lease has its persisted IP relearned WITHOUT a
    // successful session — closing the "must rescan to reconnect" gap. Never
    // adds an un-remembered satellite. Mirrors dish-android's
    // store.refreshFromDiscovery in SatelliteConnectionManager.startDiscovery.
    void refreshFromDiscovery(const QList<models::DiscoveredServer>& discovered) {
        facade_->refreshFromDiscovery(discovered);
    }
    void forget(const QString& id) { facade_->forgetSatellite(id); }

    std::optional<QString> sharedKey(const QString& id) const {
        return facade_->satelliteSharedKey(id);
    }
    void setSharedKey(const QString& keyHex, const QString& id) {
        facade_->setSatelliteSharedKey(id, keyHex);
    }
    void forgetKey(const QString& id) { facade_->forgetSatelliteSharedKey(id); }

    // The underlying facade — new code wires the TLS pin store + identity
    // consolidation through here.
    repository::ConnectionStore& facade() { return *facade_; }

  private:
    std::unique_ptr<repository::ConnectionStore> facade_;
};

} // namespace dish::net
