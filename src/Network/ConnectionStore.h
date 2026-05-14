// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QList>
#include <QSettings>
#include <QString>

#include <memory>
#include <optional>

namespace dish::net {

// Persistent registry of remembered connections + per-server shared keys.
// Backed by QSettings, which on Windows writes to HKCU\Software\Dish\Dish — the
// OS-native analogue of dish-android's SharedPreferences, dish-mac's
// UserDefaults, and dish-linux's ~/.config/Dish/Dish.conf. Mirrors
// dish-mac/Network/ConnectionStore.
class ConnectionStore {
  public:
    explicit ConnectionStore(std::unique_ptr<QSettings> settings = nullptr);

    QString getOrCreateDeviceId();

    QList<models::RememberedWifi> remembered() const;
    void remember(const models::DiscoveredServer& server);
    void forget(const QString& id);

    std::optional<QString> sharedKey(const QString& id) const;
    void setSharedKey(const QString& keyHex, const QString& id);

  private:
    void persist(const QList<models::RememberedWifi>& list);

    std::unique_ptr<QSettings> settings_;
};

} // namespace dish::net
