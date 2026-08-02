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

#include <array>
#include <cstdint>
#include <optional>

class QTimer;

namespace dish::net {

enum class ConnectionEventKind { PairingRequired, Error };

// The reverse (host-initiated) pairing lifecycle the Connections dialog binds a
// PIN sheet against. The dish shows the clientPin, the operator types it on the
// satellite, and the poll loop resolves to Approved/Declined/TimedOut. Idle is
// the resting state (no reverse pair in flight); the terminal arms are sticky
// until the next requestReversePairing/cancelReversePairing clears them.
enum class ReversePairingPhase { Idle, AwaitingApproval, Approved, Declined, TimedOut };

struct ConnectionEvent {
    ConnectionEventKind kind;
    models::DiscoveredServer server; // only meaningful for PairingRequired
    QString message;                 // only meaningful for Error
};

// Why a connect attempt was kicked off (mirrors dish-android ConnectIntent):
//   * UserInitiated   — the user tapped Connect. Every failure SHOULD toast.
//   * AutoReconnect    — app start / the 15 s timer. Failure MUST be silent
//                       (the row chip's Connecting → Saved/Stale flip is the cue).
//   * RetryAfterDeath  — the silent post-death backoff retry. Same silence policy.
// terminal-401 and close-notify(unpaired) stop the auto-retry curve.
enum class ConnectIntent { UserInitiated, AutoReconnect, RetryAfterDeath };

// Owns the pool of live + remembered WiFi sessions and drives the protocol-1
// control plane (REST) for each: the declarative session PUT on connect, the
// per-controller PUT/DELETE for slot changes, the GET-then-rePUT reconcile loop,
// terminal-401 handling, close-notify teardown, and the exponential reconnect
// backoff. UDP carries streams only. Mirrors dish-android
// source/connection/SatelliteConnectionManager.
class WifiConnectionManager : public QObject {
    Q_OBJECT
  public:
    explicit WifiConnectionManager(ConnectionStore* store, QObject* parent = nullptr);
    ~WifiConnectionManager() override;

    bool isScanning() const { return scanning_; }
    QList<models::DiscoveredServer> discoveredServers() const { return discovered_; }
    const QHash<QString, WifiConnection*>& connections() const { return connections_; }
    WifiConnection* get(const QString& id) const { return connections_.value(id, nullptr); }

    // Connection ids with a POST /api/pair in flight — the UI gates the
    // Connect / Pair buttons into a spinner-plus-disabled state for the
    // round-trip. Read on the Qt main thread only.
    bool isPairingInFlight(const QString& id) const { return pairingInFlight_.contains(id); }

    void startDiscovery();
    // Default intent UserInitiated — the public API the UI tap binds to.
    void connectTo(const models::DiscoveredServer& server,
                   ConnectIntent intent = ConnectIntent::UserInitiated);
    void pairWithPin(const models::DiscoveredServer& server, const QString& pin);

    // ── Reverse (host-initiated) pairing — Path B ───────────────────────────
    // Generate + display a 4-digit clientPin, POST it (the server stages a
    // pending grant), then poll GET /api/pair/status until the operator approves
    // on the satellite. On approval: adopt the key + openSession (exactly like a
    // forward Success). Resolves to Approved/Declined/TimedOut; every transition
    // emits reversePairingChanged. A second request while one is live cancels the
    // first. NOT unit-tested (drives real network); the DECISION core it leans on
    // — reducer::nextReversePairingAction — is exhaustively tested instead.
    void requestReversePairing(const models::DiscoveredServer& server);
    // Abort an in-flight reverse pair (stops the poll timer, returns to Idle).
    void cancelReversePairing();

    // Reverse-pairing state the QML sheet reads (folded through one NOTIFY).
    ReversePairingPhase reversePairingPhase() const { return reversePhase_; }
    QString reversePairingPin() const { return reversePin_; } // 4 digits to show
    QString reversePairingServerName() const { return reverseServerName_; }

    void disconnect(const QString& id);
    void forget(const QString& id);
    void autoReconnectAll();

    QList<models::RememberedWifi> remembered() const { return store_->remembered(); }

  signals:
    void poolChanged();
    // A per-connection telemetry readout moved (the 1 s latency tick). Kept
    // separate from poolChanged so the hub/AppModel rebuild cascade never runs
    // for a cosmetic figure — only the row-level consumers re-derive off it.
    void poolTelemetryChanged();
    void discoveredChanged();
    void scanningChanged();
    void pairingInFlightChanged();
    // Fired on every reverse-pairing transition (phase / pin / server name). The
    // QML surface folds it into the reactive reversePairing* properties.
    void reversePairingChanged();
    // Named `connectionEvent` (not `event`) so it doesn't shadow QObject::event.
    void connectionEvent(const dish::net::ConnectionEvent& evt);
    // Forwarded from per-connection slot apply failures so the hub can roll a
    // binding back when the server rejects a controller descriptor.
    void slotRegistrationFailed(const QString& slotId);
    // A FORWARD pair (the user typed the satellite's PIN) was rejected, with the
    // machine-readable cause: "wrongPin" | "versionMismatch" | "unreachable" |
    // "pending". Raised alongside the existing connectionEvent toast so the
    // pairing sheet can stay open and mark the field inline — the toast is for
    // transient failures, and a wrong PIN is a surface the user is still on.
    // Deliberately NOT raised from pairAndConnect: a background reconnect must
    // never pop an error into a sheet the user did not open.
    void pairingFailed(const QString& connectionId, const QString& reasonToken);

  private:
    WifiConnection* ensureConnection(const models::DiscoveredServer& server);
    void wireSlotSync(WifiConnection* conn);
    void pairAndConnect(WifiConnection* conn, const models::DiscoveredServer& server,
                        ConnectIntent intent);
    // The declarative connect: ONE PUT /api/connections carries identity, the
    // proof of the pairing key, and the FULL topology. Drives the session live.
    void openSession(WifiConnection* conn, const models::DiscoveredServer& server,
                     ConnectIntent intent);
    // GET-then-maybe-rePUT reconcile, fired when the enriched ack drifts.
    void reconcile(WifiConnection* conn, const models::DiscoveredServer& server);
    // Proactive re-key (contract §Crypto): re-PUT for fresh token/salt/key on
    // the SAME socket before the send counter can exhaust — no state blip.
    void rekey(WifiConnection* conn, const models::DiscoveredServer& server);
    // Single-slot converge while live (PUT .../controllers/{idx}).
    void syncSlot(const QString& id, const QString& slotId);
    // Slot delete while live (DELETE .../controllers/{idx}).
    void deleteSlot(const QString& id, int ctrlIdx);
    // Map an authenticated close-notify reason to the teardown follow-up.
    void handleServerClose(WifiConnection* conn, const models::DiscoveredServer& server,
                           std::uint8_t reason);
    // Bounded exponential backoff for the silent retry paths (never for
    // UserInitiated). A user tap resets the curve.
    void scheduleRetry(const models::DiscoveredServer& server, ConnectIntent intent);

    void emitErrorIfUserInitiated(ConnectIntent intent, const QString& message);
    void markStale(const QString& id);

    // Reverse-pairing internals. pollReverseStatus does ONE pairStatus round-trip
    // off the thread pool, feeds its classifyApproval verdict + the elapsed clock
    // through reducer::nextReversePairingAction, and acts on the result (re-arm
    // the timer / openSession / abort). setReversePhase mutates the state slice
    // and emits reversePairingChanged once. finishReverse parks a terminal arm.
    void pollReverseStatus();
    void setReversePhase(ReversePairingPhase phase);
    void finishReverse(ReversePairingPhase terminal);

    // The pairing key + proof for an authed REST call; nullopt when the key is
    // absent or undecodable (both mean: re-pair). The proof = hmacProof
    // (core/wire) of the stored pairing key.
    struct Credentials {
        std::array<std::uint8_t, 32> pairingKey{};
        QString proof;
    };
    std::optional<Credentials> credentialsFor(const QString& id) const;
    // On a terminal 401 / close-notify(unpaired): drop the key, park Stale, stop
    // retrying. Centralised so every REST path treats it identically.
    void onTerminalAuthFailure(WifiConnection* conn, const QString& id, ConnectIntent intent);

    ConnectionStore* store_;
    HTTPClient* http_;
    QString deviceId_;
    QString deviceName_;

    QHash<QString, WifiConnection*> connections_;
    QList<models::DiscoveredServer> discovered_;
    bool scanning_ = false;
    QSet<QString> pairingInFlight_;
    // Consecutive silent-retry count per id, driving the backoff. Reset on a
    // successful session or any user action.
    QHash<QString, int> retryAttempts_;
    // Single-flight reconcile guard per id (ack ticks every second; the GET can
    // take longer).
    QSet<QString> reconcileInFlight_;

    // ── Reverse-pairing state ────────────────────────────────────────────────
    // The phase the QML sheet renders, the 4-digit clientPin to display, and the
    // server we are pairing (kept so the poll loop + openSession have the
    // endpoint and so the sheet can name it). reverseTimer_ drives the poll;
    // reverseDeadlineMs_/reverseElapsedMs_ feed the pure deadline decision. The
    // QFutureWatcher* is tracked so a cancel/restart tears a stale poll's watcher
    // down without acting on its late reply.
    ReversePairingPhase reversePhase_ = ReversePairingPhase::Idle;
    QString reversePin_;
    QString reverseServerName_;
    models::DiscoveredServer reverseServer_;
    QTimer* reverseTimer_ = nullptr;
    std::int64_t reverseElapsedMs_ = 0;
    std::int64_t reverseDeadlineMs_ = 0;
    // Whether any poll in the CURRENT attempt saw "pending" — a later "none"
    // then means the operator's deny erased the row (terminal), while a "none"
    // before any pending tolerates the POST→first-poll race. Reset per attempt.
    bool reverseSawPending_ = false;
    bool reversePollInFlight_ = false;
};

} // namespace dish::net
