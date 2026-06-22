// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// RememberedSatelliteRepository — the durable list of remembered satellites.
//
// Unlike the per-key pin/shared-key repos, the remembered set is stored as ONE
// JSON array under the "satellite_list" key of the shared connection-store
// QSettings (a satellite re-appearing is a read-modify-write of the whole list,
// keyed by its stable id). Dumb synchronous storage: one std::mutex guards the
// read-modify-write; no Observables inside. A KeyedRepository<QString,
// RememberedWifi> whose keyOf() is the row's stable id. Mirrors dish-android
// repository/RememberedSatelliteRepository.kt (its RememberedSatellite is this
// client's models::RememberedWifi).
//
// Corrupt persisted JSON falls back to an empty list rather than crashing — an
// old/garbled blob must not brick discovery.

#pragma once

#include "Models/Models.h"
#include "architecture/Repository.h"
#include "repository/RememberedWifiEq.h"

#include <QList>
#include <QMap>
#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

class RememberedSatelliteRepository
    : public arch::KeyedRepository<QString, models::RememberedWifi> {
  public:
    explicit RememberedSatelliteRepository(std::shared_ptr<QSettings> settings = nullptr);

    QString keyOf(const models::RememberedWifi& value) const override { return value.id; }

    std::optional<models::RememberedWifi> get(const QString& id) const override;
    std::vector<models::RememberedWifi> all() const override;
    void put(const QString& id, const models::RememberedWifi& value) override;
    void remove(const QString& id) override;
    void clear() override;

    // Pull up the KeyedRepository value-overloads (put(value)/removeValue(value))
    // hidden by the get/put/remove declarations above.
    using arch::KeyedRepository<QString, models::RememberedWifi>::put;
    using arch::KeyedRepository<QString, models::RememberedWifi>::removeValue;

  private:
    // Read/write the storage-key -> row map. Both assume mutex_ is held.
    QMap<QString, models::RememberedWifi> readEntries() const;
    void writeEntries(const QMap<QString, models::RememberedWifi>& entries);

    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
