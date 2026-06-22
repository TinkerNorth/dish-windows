// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteSharedKeyRepository — per-satellite pairing-key store.
//
// Stores the hex pairing key established during pairing, under the
// "satellite_shared_key:<id>" namespace of the shared connection-store
// QSettings. Dumb synchronous storage with one std::mutex; no Observables
// inside. The session manager reads the key to derive a session key; identity
// consolidation migrates it across address/identity changes without a re-pair.
// Mirrors dish-android repository/SatelliteSharedKeyRepository.kt.
//
// Co-tenants the cert-pin repo and remembered list in one QSettings file; kept
// disjoint by key prefix. all()/clear() touch only this repo's prefix, so a
// shared key never leaks into the pin namespace and clear() preserves siblings.

#pragma once

#include "architecture/Repository.h"

#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

class SatelliteSharedKeyRepository : public arch::Repository<QString, QString> {
  public:
    explicit SatelliteSharedKeyRepository(std::shared_ptr<QSettings> settings = nullptr);

    std::optional<QString> get(const QString& id) const override;
    std::vector<QString> all() const override;
    void put(const QString& id, const QString& keyHex) override;
    void remove(const QString& id) override;
    void clear() override;

  private:
    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
