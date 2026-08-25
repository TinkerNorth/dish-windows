// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Durable storage for the Moonlight path: the client's persistent identity
// (self-signed cert + key), the remembered host list, and the per-host pinned
// server certificate. Co-tenants the shared connection-store QSettings file,
// kept disjoint from the Satellite keys by the prefixes in SettingsKeys.h.
//
// The private key is stored as PLAINTEXT PEM in the user's hive, no DPAPI wrap,
// the same documented trade-off as the Satellite pairing key (see PRIVACY.md /
// SECURITY.md). It only authorises input to a host the user has already paired.

#pragma once

#include "Network/MoonlightHost.h"
#include "core/moonlight/MoonlightIdentity.h"

#include <QList>
#include <QSettings>
#include <QString>

#include <memory>
#include <mutex>
#include <optional>

namespace dish::repository {

class MoonlightHostRepository {
  public:
    // nullptr backs it on the shared HKCU store; tests pass a temp QSettings.
    explicit MoonlightHostRepository(std::shared_ptr<QSettings> settings = nullptr);

    // The client's Moonlight identity, generated and persisted on first call.
    // nullopt only if generation failed (an OpenSSL error).
    std::optional<moonlight::Identity> getOrCreateIdentity();

    QList<models::MoonlightHost> hosts() const;
    void rememberHost(const models::MoonlightHost& host);
    void forgetHost(const QString& id);

    // Per-host TOFU pin of the server certificate PEM.
    std::optional<QString> serverCert(const QString& id) const;
    void setServerCert(const QString& id, const QString& certPem);

  private:
    QList<models::MoonlightHost> readHosts() const; // assumes mutex_ held
    void writeHosts(const QList<models::MoonlightHost>& hosts);

    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
