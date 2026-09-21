// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteSharedKeyRepository — per-satellite pairing-key store, under the
// "satellite_shared_key:<id>" namespace of the shared connection-store
// QSettings. The session manager reads the key to derive a session key.
//
// The key is stored wrapped by the platform cipher (DPAPI, scoped to the
// Windows account: repository/SecretValue.h for the stored shape), the same
// reach the other clients give it. `cipher` is injected so the tests can see
// the stored shape through a reversible fake; a null one means the real DPAPI.
// A value an older build wrote in plaintext is wrapped in place on
// construction, so nobody re-pairs for this.
//
// A wrapped value the platform will not open (another account's hive, a
// tampered value) reads as ABSENT, never as garbage: the session then fails
// pairing-required and the user pairs again, which is the honest outcome. A
// protect that fails (which DPAPI does not do for a live account) stores the
// plaintext rather than dropping the key, the same choice dish-linux makes
// when no keyring answers.
//
// Co-tenants the cert-pin repo and remembered list in one QSettings file, kept
// disjoint by key prefix, so all()/clear() must stay prefix-scoped: a shared key
// must never leak into the pin namespace.

#pragma once

#include "architecture/Repository.h"
#include "source/system/SecretCipher.h"

#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

class SatelliteSharedKeyRepository : public arch::Repository<QString, QString> {
  public:
    explicit SatelliteSharedKeyRepository(std::shared_ptr<QSettings> settings = nullptr,
                                          std::shared_ptr<source::SecretCipher> cipher = nullptr);

    std::optional<QString> get(const QString& id) const override;
    std::vector<QString> all() const override;
    void put(const QString& id, const QString& keyHex) override;
    void remove(const QString& id) override;
    void clear() override;

  private:
    // Wraps every plaintext value under the prefix. Assumes mutex_ held.
    void wrapLegacyValues();
    std::optional<QString> readKey(const QString& settingsKey) const; // assumes mutex_ held

    std::shared_ptr<QSettings> settings_;
    std::shared_ptr<source::SecretCipher> cipher_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
