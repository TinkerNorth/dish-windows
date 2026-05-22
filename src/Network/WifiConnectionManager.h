// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "ConnectionStore.h"
#include "HTTPClient.h"
#include "Models/Models.h"
#include "WifiConnection.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

namespace dish::net {

enum class ConnectionEventKind { PairingRequired, Error, Warning };

struct ConnectionEvent {
    ConnectionEventKind kind;
    models::DiscoveredServer server; // only meaningful for PairingRequired
    QString message;                 // only meaningful for Error / Warning
};

// Why a connect attempt was kicked off. The same connectTo / pairAndConnect
// plumbing is shared between three callers with very different user-feedback
// expectations (mirrors dish-android's SatelliteConnectionManager.ConnectIntent):
//
//   * UserInitiated   — the user tapped Connect on a discovered row. Every
//                       failure path SHOULD surface a toast — they just asked
//                       for the action and would otherwise see no signal.
//   * AutoReconnect   — fired on app start / from the 15 s auto-reconnect
//                       timer. Failure MUST be silent: the row chip's natural
//                       Connecting → Saved/Stale flip is the feedback. A toast
//                       on every cold start the satellite is offline is pure
//                       noise.
//   * RetryAfterDeath — fired by the alive-poll's onDead path after a short
//                       backoff. Same silence policy as AutoReconnect.
//
// Threaded through pairAndConnect / openSession; every error emission gates
// on it via emitErrorIfUserInitiated().
enum class ConnectIntent { UserInitiated, AutoReconnect, RetryAfterDeath };

// Owns the pool of live + remembered WiFi sessions. Each session runs its own
// native socket, heartbeat and ACK loop so multiple servers can be active in
// parallel. Mirrors dish-mac/Network/WifiConnectionManager.swift.
class WifiConnectionManager : public QObject {
    Q_OBJECT
  public:
    explicit WifiConnectionManager(ConnectionStore* store, QObject* parent = nullptr);
    ~WifiConnectionManager() override;

    bool isScanning() const { return scanning_; }
    QList<models::DiscoveredServer> discoveredServers() const { return discovered_; }
    const QHash<QString, WifiConnection*>& connections() const { return connections_; }
    WifiConnection* get(const QString& id) const { return connections_.value(id, nullptr); }

    // Set of connection ids whose POST /api/pair is currently in flight.
    // Mirrors dish-mac's `pairingInFlight`. Used by the UI to gate the
    // Connect / Pair buttons into a spinner-plus-disabled state for the
    // duration of the round-trip so the user has continuous visual feedback
    // through `pair -> openSession -> markConnected` rather than a brief
    // un-disabled gap. Read on the Qt main thread only.
    bool isPairingInFlight(const QString& id) const { return pairingInFlight_.contains(id); }

    void startDiscovery();
    // Default intent is UserInitiated — `connectTo` is the public API the UI
    // tap binds to. Auto-reconnect and the post-onDead silent-retry path go
    // through the same plumbing with AutoReconnect / RetryAfterDeath.
    void connectTo(const models::DiscoveredServer& server,
                   ConnectIntent intent = ConnectIntent::UserInitiated);
    void pairWithPin(const models::DiscoveredServer& server, const QString& pin);
    void disconnect(const QString& id);
    void forget(const QString& id);
    void autoReconnectAll();

    QList<models::RememberedWifi> remembered() const { return store_->remembered(); }

  signals:
    void poolChanged();
    void discoveredChanged();
    void scanningChanged();
    // Emitted when a connection id is inserted into or removed from
    // `pairingInFlight_`. The dialog listens for this to refresh the
    // in-flight spinner inside the Connect / Pair button. Mirrors dish-mac's
    // `@Published pairingInFlight` change-notification semantics.
    void pairingInFlightChanged();
    // Named `connectionEvent` (not `event`) so the signal does not shadow
    // QObject::event(QEvent*), which clang flags with
    // -Wclang-diagnostic-overloaded-virtual.
    void connectionEvent(const dish::net::ConnectionEvent& evt);
    // Forwarded from per-connection WifiConnection::registrationFailed so
    // ConnectionHub can roll back a binding when the server rejects a
    // controller add.
    void slotRegistrationFailed(const QString& slotId);

  private:
    WifiConnection* ensureConnection(const models::DiscoveredServer& server);
    void pairAndConnect(WifiConnection* conn, const models::DiscoveredServer& server,
                        const QString& pin, ConnectIntent intent);
    void openSession(WifiConnection* conn, const models::DiscoveredServer& server,
                     ConnectIntent intent);
    // Emit a ConnectionEventKind::Error toast only when the user has a recent
    // mental model for "I asked for this action". On AutoReconnect /
    // RetryAfterDeath the row chip already conveys the result (Connecting →
    // Saved / Stale / Online) and a toast on top is noise.
    void emitErrorIfUserInitiated(ConnectIntent intent, const QString& message);
    // Silent reconnect after a brief backoff. Fired from the alive-poll's
    // onDead callback so a momentary Wi-Fi blip self-heals before the user
    // notices, without bouncing the satellite with back-to-back retries.
    void scheduleSilentRetry(const QString& id);

    ConnectionStore* store_;
    HTTPClient* http_;
    QString deviceId_;
    QString deviceName_;

    QHash<QString, WifiConnection*> connections_;
    QList<models::DiscoveredServer> discovered_;
    bool scanning_ = false;
    // Connection ids with an in-flight POST /api/pair. Inserted as soon as
    // the future starts, removed in every terminal branch (success, auth
    // error, unreachable). UI gate: spinner inside the button + disable.
    QSet<QString> pairingInFlight_;
};

} // namespace dish::net
