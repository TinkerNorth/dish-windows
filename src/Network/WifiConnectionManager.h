// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "core/reducer/Reconcile.h"
#include "core/reducer/RestOutcome.h"
#include "core/reducer/ReversePairing.h"
#include "ConnectionStore.h"
#include "HTTPClient.h"
#include "Models/Models.h"
#include "PairingOutcome.h"
#include "WifiConnection.h"
#include "WifiManagerEffects.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace dish::net {

enum class ConnectionEventKind { PairingRequired, Error };

// Reverse (host-initiated) pairing: the dish shows a clientPin, the operator
// types it on the satellite, and the poll loop resolves. The terminal arms are
// sticky until the next request or cancel clears them. IdentityChanged and
// VersionMismatch end an attempt no new code can rescue, so each keeps its own
// arm rather than reading as the operator's decline.
enum class ReversePairingPhase {
    Idle,
    AwaitingApproval,
    Approved,
    Declined,
    TimedOut,
    IdentityChanged,
    VersionMismatch
};

struct ConnectionEvent {
    ConnectionEventKind kind;
    models::DiscoveredServer server; // only meaningful for PairingRequired
    QString message;                 // only meaningful for Error
};

// Only UserInitiated may toast on failure. The two background intents must fail
// silently, with the row chip's Connecting → Saved/Stale flip as the only cue.
enum class ConnectIntent { UserInitiated, AutoReconnect, RetryAfterDeath };

// Owns the pool of live and remembered WiFi sessions and drives the REST control
// plane for each: session PUT on connect, per-controller PUT/DELETE for slot
// changes, the GET-then-rePUT reconcile, terminal-401 handling, close-notify
// teardown, and the reconnect backoff. UDP carries streams only.
class WifiConnectionManager : public QObject {
    Q_OBJECT
  public:
    explicit WifiConnectionManager(ConnectionStore* store, QObject* parent = nullptr);
    // Takes ownership of `http`. A test hands in an in-process REST API and its own effects, so
    // the manager runs on virtual time with no network.
    WifiConnectionManager(ConnectionStore* store, HTTPClient* http, WifiManagerEffects effects,
                          QObject* parent);
    ~WifiConnectionManager() override;

    bool isScanning() const { return scanning_; }
    QList<models::DiscoveredServer> discoveredServers() const { return discovered_; }
    const QHash<QString, WifiConnection*>& connections() const { return connections_; }
    WifiConnection* get(const QString& id) const { return connections_.value(id, nullptr); }

    // Main thread only.
    bool isPairingInFlight(const QString& id) const { return pairingInFlight_.contains(id); }

    void startDiscovery();
    void connectTo(const models::DiscoveredServer& server,
                   ConnectIntent intent = ConnectIntent::UserInitiated);
    void pairWithPin(const models::DiscoveredServer& server, const QString& pin);

    // Posts a generated clientPin, then polls /api/pair/status until the operator
    // approves. On approval it adopts the key and opens the session exactly like
    // a forward pair. A second request while one is live cancels the first.
    // The decision core it leans on, reducer::nextReversePairingAction, is tested
    // on its own.
    void requestReversePairing(const models::DiscoveredServer& server);

    // Path B's steps: draw the PIN, arm the attempt, then classify whatever the POST comes back
    // with against the attempt that is still on screen.
    static QString drawReversePin();
    void armReverseAttempt(const models::DiscoveredServer& server);
    bool reverseAttemptIsCurrent(const models::DiscoveredServer& server, const QString& pin) const;
    void startReversePoll();
    void applyReverseOutcome(const models::DiscoveredServer& server,
                             const models::PairResponse& pair, bool pinMismatch);
    // `sentOn` is the connection the POST went out on.
    void onReversePairReply(const QString& id, const QPointer<WifiConnection>& sentOn,
                            const models::DiscoveredServer& server, const QString& pin,
                            const models::PairResponse& pair, bool pinMismatch);
    void cancelReversePairing();

    ReversePairingPhase reversePairingPhase() const { return reversePhase_; }
    QString reversePairingPin() const { return reversePin_; }
    QString reversePairingServerName() const { return reverseServerName_; }

    // The user's Disconnect: the session closes, and no background intent reconnects this
    // satellite until the user connects it again (or forgets it). Session-scoped, not persisted.
    void disconnect(const QString& id);
    void forget(const QString& id);
    void autoReconnectAll();
    // Suspend/resume. Tearing down first matters: a session the machine slept
    // through comes back Faltering, which slips past both autoReconnectAll's
    // "not Live" guard and the heartbeat-death reconnect, so nothing would ever
    // reopen it. Closing every session on the way down and reopening from the
    // remembered list on the way back is the only path that always converges.
    void prepareForSleep();
    void resumeFromSleep();

    QList<models::RememberedWifi> remembered() const { return store_->remembered(); }

  signals:
    void poolChanged();
    // Kept separate from poolChanged so the hub and AppModel rebuild cascade never
    // runs for a cosmetic 1 Hz figure.
    void poolTelemetryChanged();
    void discoveredChanged();
    void scanningChanged();
    void pairingInFlightChanged();
    void reversePairingChanged();
    // Not named `event`, which would shadow QObject::event.
    void connectionEvent(const dish::net::ConnectionEvent& evt);
    // Lets the hub roll a binding back when the server rejects a descriptor.
    void slotRegistrationFailed(const QString& slotId);
    // A rejected FORWARD pair, with `reasonToken` one of "wrongPin" |
    // "versionMismatch" | "unreachable" | "pending". Separate from the toast so
    // the pairing sheet can stay open and mark the field inline. Deliberately not
    // raised from pairAndConnect: a background reconnect must never pop an error
    // into a sheet the user did not open.
    void pairingFailed(const QString& connectionId, const QString& reasonToken);

  private:
    bool refusesHost(const models::DiscoveredServer& server, ConnectIntent intent);
    bool heldByUser(const QString& id, ConnectIntent intent);
    // Whether a key is on file for a satellite at `host`: what a pin there stands in front of.
    // Every request goes out on a connection, so the connections are where to look.
    bool pinGuardsAPairingAt(const QString& host) const;
    // Closes the local side and releases the session, without the user's hold.
    void closeSession(const QString& id);
    void onVersionRejected(WifiConnection* conn, const models::DiscoveredServer& server,
                           ConnectIntent intent, const models::SessionResponse& resp);
    void onDiscoveryFinished(const QList<models::DiscoveredServer>& found);
    WifiConnection* ensureConnection(const models::DiscoveredServer& server);
    void wireSlotSync(WifiConnection* conn);
    void pairAndConnect(WifiConnection* conn, const models::DiscoveredServer& server,
                        ConnectIntent intent);
    // The forward pairs' replies: a connect's PIN-less one, and the operator's PIN from the sheet.
    // `sentOn` is the connection the request went out on.
    void onConnectPairReply(const QString& id, const QPointer<WifiConnection>& sentOn,
                            const models::DiscoveredServer& server, ConnectIntent intent,
                            const PairingOutcome::Arm& outcome);
    void onPinPairReply(const QString& id, const QPointer<WifiConnection>& sentOn,
                        const models::DiscoveredServer& server, const PairingOutcome::Arm& outcome);
    // What a reply for `id` may act on: the connection its request went out on, while that is still
    // the one filed under the id. A Forget in between leaves none, and a Forget and a fresh pair
    // leave a newer one, which an older reply must not reach. Null for either.
    WifiConnection* replyTarget(const QString& id, const QPointer<WifiConnection>& sentOn) const;
    // A pairing reply ends its own request, unless a Forget and a fresh pair have since filed a
    // newer connection under the id, whose own request the flag now stands for.
    void endPairingRequest(const QString& id, const QPointer<WifiConnection>& sentOn);
    void adoptReverseGrant(const models::DiscoveredServer& server, const QString& sharedKeyHex);

    // nullopt when the stored key is absent or undecodable, both of which mean
    // re-pair.
    struct Credentials {
        std::array<std::uint8_t, 32> pairingKey{};
        QString proof;
    };
    std::optional<Credentials> credentialsFor(const QString& id) const;

    // One PUT /api/connections carrying identity, the key proof and the FULL
    // topology, which is what drives the session live.
    void openSession(WifiConnection* conn, const models::DiscoveredServer& server,
                     ConnectIntent intent);
    // What a granted PUT carries to key the session with: the token the satellite assigned, and
    // the per-session key derived from it, the salt and the pairing key. The pairing key itself
    // never reaches the UDP path; only this derived key does. Null when the token or the salt is
    // missing or malformed. The connect and the rekey read it the same way.
    struct SessionMaterial {
        std::array<std::uint8_t, 4> token{};
        std::array<std::uint8_t, 32> sessionKey{};
    };
    static std::optional<SessionMaterial>
    sessionMaterialFrom(const models::SessionResponse& resp,
                        const std::array<std::uint8_t, 32>& pairingKey);
    // The connect PUT's reply, in steps: a refusal settles itself, a grant this end cannot carry is
    // handed back, and a usable one starts the session and then converges the slot changes that
    // raced the round trip. `sent` is what the PUT was sent with.
    void onSessionReply(const QString& id, const QPointer<WifiConnection>& sentOn,
                        const models::DiscoveredServer& server, ConnectIntent intent,
                        const Credentials& sent,
                        const std::vector<reducer::DesiredSlot>& sentDescriptors,
                        const models::SessionResponse& resp, bool pinMismatch);
    // The attempt a PUT answers already ended here, by a user's Disconnect or a sleep. A session
    // the satellite granted meanwhile is handed straight back rather than left holding a slot until
    // its own timeout; any other answer has nobody left to tell.
    void handBackLateGrant(const models::DiscoveredServer& server,
                           const models::SessionResponse& resp, const QString& proof);
    void onSessionRefused(WifiConnection* conn, const models::DiscoveredServer& server,
                          ConnectIntent intent, reducer::RestVerdict verdict,
                          const models::SessionResponse& resp);
    // `connectionId` rides beside `resp` because onSessionReply is where its presence is checked.
    void startSession(WifiConnection* conn, const models::DiscoveredServer& server,
                      const std::shared_ptr<SatelliteClient>& client, const QString& connectionId,
                      const models::SessionResponse& resp, const SessionMaterial& material);
    void convergeLateSlots(WifiConnection* conn, const std::vector<reducer::DesiredSlot>& sent);
    // GET-then-maybe-rePUT, fired when the enriched ack drifts.
    void reconcile(WifiConnection* conn, const models::DiscoveredServer& server);
    // Re-PUT for a fresh token/salt/key on the SAME socket, so there is no state
    // blip visible to the UI.
    void rekey(WifiConnection* conn, const models::DiscoveredServer& server);

    // The satellite granted a session the link here cannot use (unusable material, or a socket
    // that will not open). It is handed back rather than left holding a slot until the
    // satellite's own timeout, a user's connect is told, and nothing retries: the same grant
    // would fail the same way.
    void releaseUnusableGrant(WifiConnection* conn, const models::DiscoveredServer& server,
                              const QString& connectionId, const QString& proof,
                              ConnectIntent intent);
    // DELETE /api/connections/{id}, best-effort: the local side already treats it as gone.
    void releaseSession(const models::DiscoveredServer& server, const QString& connectionId,
                        const QString& proof);

    // The rekey PUT's reply, in three steps: is this still the session that asked, does the reply
    // carry material, and adopt it.
    void onRekeyReply(const QString& id, const std::shared_ptr<SatelliteClient>& client,
                      const std::array<std::uint8_t, 32>& pairingKey,
                      const models::SessionResponse& resp, bool pinMismatch);
    void adoptRekey(WifiConnection* c, const QString& id,
                    const std::shared_ptr<SatelliteClient>& client,
                    const models::SessionResponse& resp, const SessionMaterial& material);
    // Reads GET /api/server/capabilities for the host's controller-audio
    // verdict and folds it into the connection (reducer/HostAudioVerdict.h).
    // Fired after EVERY successful session PUT — connect, reconnect-after-death
    // and the proactive re-key alike — because the verdict is live host state
    // and a fresh session is exactly when it may have moved. Probing only from
    // a UI surface was dish-android's mistake: an auto-reconnected session then
    // streams with the verdict stuck at the conservative default until the user
    // happens to open that screen.
    void probeHostAudio(const QString& id, const models::DiscoveredServer& server);
    void syncSlot(const QString& id, const QString& slotId);
    void deleteSlot(const QString& id, int ctrlIdx);
    void handleServerClose(WifiConnection* conn, const models::DiscoveredServer& server,
                           std::uint8_t reason);
    // Never runs for UserInitiated; a user tap resets the curve.
    void scheduleRetry(const models::DiscoveredServer& server, ConnectIntent intent);
    void onRetryDue(const QString& id, const models::DiscoveredServer& server);
    QObject* retryScopeFor(const QString& id);
    void cancelPendingRetries(const QString& id);

    void emitErrorIfUserInitiated(ConnectIntent intent, const QString& message);
    void markStale(const QString& id);

    // One pairStatus round-trip, fed with the elapsed clock through
    // reducer::nextReversePairingAction to decide re-arm / open / abort.
    void pollReverseStatus();

    // One approval poll, in order: what the reply says, and what that answer means.
    static reducer::ApprovalReply approvalReplyOf(const models::PairResponse& status);
    void onReverseStatusReply(const models::PairResponse& status, bool pinMismatch,
                              const models::DiscoveredServer& server);
    void applyReverseAction(reducer::ReversePairingAction action,
                            const models::PairResponse& status,
                            const models::DiscoveredServer& server);
    void setReversePhase(ReversePairingPhase phase);
    void finishReverse(ReversePairingPhase terminal);

    // Drops the key, parks Stale and stops retrying. Centralised so every REST
    // path treats a terminal auth failure identically.
    void onTerminalAuthFailure(WifiConnection* conn, const QString& id, ConnectIntent intent);

    ConnectionStore* store_;
    HTTPClient* http_;
    WifiManagerEffects effects_;
    QString deviceId_;
    QString deviceName_;

    // A reply looks its connection up here by id and acts on it only if it is still the object the
    // request went out on: forget() hands a connection to deleteLater, so a pointer held across a
    // round trip can dangle, and a Forget followed by a fresh pair files a new object under the
    // same id, which a reply from before the Forget must not reach.
    QHash<QString, WifiConnection*> connections_;
    QList<models::DiscoveredServer> discovered_;
    bool scanning_ = false;
    QSet<QString> pairingInFlight_;
    // Drives the backoff. Reset on a successful session or any user action.
    QHash<QString, int> retryAttempts_;
    // Per satellite, the context every pending silent retry is armed under. Children of this
    // manager; a user's disconnect deletes one to cancel its retries.
    QHash<QString, QObject*> retryScopes_;
    // Satellites the user disconnected, which only a user connect or a forget releases.
    QSet<QString> userDisconnected_;
    // Single-flight guard: the ack ticks every second but the GET can take longer.
    QSet<QString> reconcileInFlight_;

    ReversePairingPhase reversePhase_ = ReversePairingPhase::Idle;
    QString reversePin_;
    QString reverseServerName_;
    models::DiscoveredServer reverseServer_;
    QTimer* reverseTimer_ = nullptr;
    std::int64_t reverseElapsedMs_ = 0;
    std::int64_t reverseDeadlineMs_ = 0;
    // Disambiguates a "none" reply: after a pending, it means the operator's deny
    // erased the row and is terminal; before one, it is just the POST-to-first-poll
    // race and is tolerated. Reset per attempt.
    bool reverseSawPending_ = false;
    bool reversePollInFlight_ = false;
};

} // namespace dish::net
