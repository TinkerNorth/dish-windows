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

    // The uniqueid every GameStream request carries, minted once per install.
    // Moonlight clients traditionally share one constant here, but a host keys
    // its pending pairings and session ownership on it, so two installs behind
    // one value collide the moment both pair with the same host. The other two
    // Dish clients mint theirs; this is the same sixteen hex digits.
    QString getOrCreateUniqueId();

    QList<models::MoonlightHost> hosts() const;
    void rememberHost(const models::MoonlightHost& host);
    void forgetHost(const QString& id);

    // Per-host TOFU pin of the server certificate PEM.
    std::optional<QString> serverCert(const QString& id) const;
    void setServerCert(const QString& id, const QString& certPem);
    // Drop the pin while keeping the host. The answer to a host that has
    // announced a new identity: the pin we hold is the OLD host's, so refusing
    // its certificate protects nothing and would leave the user with no way to
    // pair again at all. Forgetting the host clears it too.
    void clearServerCert(const QString& id);

    // The standing bindings. A binding survives the host being unreachable and
    // the pairing being lost, because it is an intent rather than a connection.
    QList<models::MoonlightBinding> bindings() const;
    std::optional<models::MoonlightBinding> binding(const QString& slotId) const;
    void rememberBinding(const models::MoonlightBinding& binding);
    void forgetBinding(const QString& slotId);
    // Every binding that drove one host. A binding is an intent to drive THAT
    // host, so forgetting the host retires it rather than leaving it pointing at
    // a pairing that no longer exists.
    void forgetBindingsForHost(const QString& hostId);

  private:
    QList<models::MoonlightHost> readHosts() const; // assumes mutex_ held
    void writeHosts(const QList<models::MoonlightHost>& hosts);
    QList<models::MoonlightBinding> readBindings() const; // assumes mutex_ held
    void writeBindings(const QList<models::MoonlightBinding>& bindings);

    std::shared_ptr<QSettings> settings_;
    mutable std::mutex mutex_;
};

} // namespace dish::repository
