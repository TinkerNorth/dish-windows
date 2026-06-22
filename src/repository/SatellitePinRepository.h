// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatellitePinRepository — per-satellite TLS cert-pin store (TOFU anchor).
//
// Stores the SHA-256 fingerprint the dish pinned for each satellite id, under
// the "satellite_cert_pin:<id>" key namespace of the shared connection-store
// QSettings (HKCU\Software\Dish\Dish). Dumb synchronous storage: one std::mutex
// guarding read-modify-write, no Observables/events inside (wrap in a Source for
// reactive reads). The TLS callback in source/http consults this via
// core/net/Tofu; identity consolidation in ConnectionStore migrates pins across
// address changes. Mirrors dish-android repository/SatellitePinRepository.kt.
//
// Namespace isolation is a hard invariant: a pin must never surface as a shared
// key (and vice versa). The two repos co-tenant one QSettings file and are kept
// disjoint purely by key prefix; all()/clear() touch only this repo's prefix.

#pragma once

#include "architecture/Repository.h"

#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

class SatellitePinRepository : public arch::Repository<QString, QString> {
  public:
    // `settings` lets tests inject an in-memory-style shared store; production
    // passes the shared connection-store QSettings so the pin/key/list repos
    // co-tenant one backing file. nullptr → the default HKCU store.
    explicit SatellitePinRepository(std::shared_ptr<QSettings> settings = nullptr);

    // Domain-named convenience over the generic surface.
    std::optional<QString> pinnedFingerprint(const QString& id) const { return get(id); }
    void pin(const QString& id, const QString& fingerprintHex) { put(id, fingerprintHex); }
    void forget(const QString& id) { remove(id); }

    // Repository<QString,QString> surface.
    std::optional<QString> get(const QString& id) const override;
    std::vector<QString> all() const override;
    void put(const QString& id, const QString& fingerprintHex) override;
    void remove(const QString& id) override;
    void clear() override;

  private:
    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
