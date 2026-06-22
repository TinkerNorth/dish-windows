// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"
#include "SatelliteClient.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace dish::net {

// Internal wire-level session state for one Satellite connection (the "Presence"
// axis). Distinct from the UI-facing [models::LinkState] (Models.h), which folds
// pairing/discovery in on top of this. Mirrors dish-android SatelliteSessionState.
//
// - Idle       — no live session (paired or not).
// - Linking    — session PUT / auth handshake in flight. UI chip: "Connecting…".
// - Live       — UDP socket open, heartbeat acks flowing. UI chip: "Online".
// - Faltering  — Live, heartbeat-miss count non-zero and below the death
//                threshold. UI chip: "Unsteady". Not yet entered (the native
//                alive-poll only exposes a binary isAlive()).
// - Stale      — the session collapsed but the chip reads "Needs pairing": a
//                terminal 401 / close-notify(unpaired) dropped the key, or a
//                silent retry is in flight. Mirrors android's staleSatelliteIds.
enum class SessionState { Idle, Linking, Live, Faltering, Stale };

// Thread-safe holder for the live SatelliteClient pointer. Writes from the Qt
// main thread (markConnected/markDisconnected); reads from the SDL gamepad
// thread on every report. Guarded by std::mutex.
class ClientRef {
  public:
    std::shared_ptr<SatelliteClient> get() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return value_;
    }
    void set(std::shared_ptr<SatelliteClient> v) {
        std::lock_guard<std::mutex> lock(mtx_);
        value_ = std::move(v);
    }

  private:
    mutable std::mutex mtx_;
    std::shared_ptr<SatelliteClient> value_;
};

// A single live or potential WiFi session to one Satellite server. Slots are
// DECLARATIVE (contract §Session): the connection holds the desired descriptor
// per slot plus the applied state the satellite last confirmed. Topology rides
// REST (session PUT on connect, per-controller PUT/DELETE while live) — UDP
// never mutates it. Mirrors dish-android source/connection/SatelliteConnection.
class WifiConnection : public QObject {
    Q_OBJECT
  public:
    static QString idFor(const models::DiscoveredServer& s) { return s.id(); }

    WifiConnection(QString id, models::DiscoveredServer server, QObject* parent = nullptr);
    ~WifiConnection() override;

    QString id() const { return id_; }
    const models::DiscoveredServer& server() const { return server_; }
    SessionState state() const { return state_; }
    std::optional<QString> connectionId() const { return connectionId_; }
    std::optional<QString> boundSlotId() const { return boundSlotId_; }
    std::shared_ptr<SatelliteClient> client() const { return clientRef_.get(); }

    // The epoch the satellite stamped on our last PUT/GET — the reference the
    // heartbeat-ack epoch is compared against. -1 until the first PUT lands.
    int lastAppliedEpoch() const { return lastAppliedEpoch_; }
    void adoptEpoch(int epoch) { lastAppliedEpoch_ = epoch; }
    // Session-level mouseControl grant from the last session PUT (the server
    // computes it only there — contract §hostFeatures).
    bool mouseControlGranted() const { return mouseControlGranted_; }

    void updateServer(const models::DiscoveredServer& s);
    void markConnecting();
    // Adopt the UDP tuple + applied state from a session PUT response. `onDead`
    // fires on heartbeat death; `onClose` on an authenticated close-notify (the
    // reason byte); `onReconcile` when the enriched-ack epoch/bitmap drifts.
    void markConnected(std::shared_ptr<SatelliteClient> client, const QString& connectionId,
                       int epoch, bool mouseControlGranted, std::function<void()> onDead,
                       std::function<void(std::uint8_t reason)> onClose,
                       std::function<void()> onReconcile);
    void markDisconnected();
    // Like markDisconnected, but lands in Stale (the "Needs pairing" chip cue)
    // instead of Idle. Used by the terminal-401 / unpaired / silent-retry paths.
    void markStale();

    // ── Declarative slots ───────────────────────────────────────────────────
    // One desired slot: ctrlIdx + the WHOLE descriptor, plus whether the
    // satellite confirmed it applied (the slot's virtual pad exists). Streams
    // are gated on `registered`.
    struct SlotBinding {
        int controllerIndex = 0;
        int controllerType = proto::kControllerTypeXbox;
        std::uint8_t touchpadMode = proto::kTouchpadModeOff;
        bool hasLightbar = false;
        bool hasMotion = false;
        bool registered = false;
    };

    // Bind a slot with its FINAL descriptor (the type travels with the attach —
    // no default-then-correct phase). While live, the manager converges it via a
    // controller PUT (`onSlotChanged`); while idle it rides the next session PUT.
    void attachSlot(const QString& slotId, int controllerType, bool hasLightbar, bool hasMotion);
    void detachSlot();
    // Detach by slotId (the hub may unbind a specific slot).
    void detachSlot(const QString& slotId);

    // The desired descriptor for a slot (whole), or nullopt if unknown.
    std::optional<models::ControllerDescriptor> descriptorFor(const QString& slotId) const;
    // All desired descriptors (the controllers[] for a session PUT).
    QList<models::ControllerDescriptor> desiredDescriptors() const;
    // Slot id owning a controller index, or empty.
    QString slotIdForIndex(int ctrlIdx) const;
    // True when any slot requests mouse touchpad mode (drives hostFeatures).
    bool wantsMouseControl() const;

    // Fold a session/controller PUT's apply results into the slot state:
    // registered=true (and motion status cached) for a live slot (ok or
    // replugFailed), registered=false otherwise. Mirrors android applyResults.
    void applyResults(const QList<models::ControllerApplyDto>& results);
    // True when the GET applied view matches our desired set (incl. the mouse
    // grant) — the caller adopts the epoch instead of re-PUTting. Pure-ish;
    // delegates the diff to the reconcile reducer.
    bool matchesAppliedView(const models::SessionViewDto& view) const;
    // The 16-bit bitmap of registered controller indices (for the ack compare).
    std::uint16_t registeredBitmap() const;

    // Hot path: called directly from the SDL gamepad thread. Gated on the bound
    // slot being registered (an unapplied descriptor is dropped server-side).
    void sendReport(std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt, std::int16_t lx,
                    std::int16_t ly, std::int16_t rx, std::int16_t ry);
    void sendMotion(std::int16_t gyroX, std::int16_t gyroY, std::int16_t gyroZ, std::int16_t accelX,
                    std::int16_t accelY, std::int16_t accelZ, std::uint32_t timestampDeltaUs);
    void sendBattery(std::uint8_t level, std::uint8_t status);
    void sendTouchpad(bool finger0Active, std::uint8_t finger0Id, std::int16_t finger0X,
                      std::int16_t finger0Y, bool finger1Active, std::uint8_t finger1Id,
                      std::int16_t finger1X, std::int16_t finger1Y, bool buttonPressed,
                      std::uint32_t eventTimeMs);

    // Per-connection rumble/lightbar handlers — stored here (not the per-session
    // SatelliteClient) so they survive reconnects; markConnected re-installs them.
    using RumbleHandler = std::function<void(const SatelliteClient::RumbleMessage&)>;
    void setRumbleHandler(RumbleHandler handler);
    using LightbarHandler = std::function<void(const SatelliteClient::LightbarMessage&)>;
    void setLightbarHandler(LightbarHandler handler);

  signals:
    void changed();
    // Transient one-shot — a controller descriptor failed to apply server-side.
    void errorOccurred(const QString& message);
    // A slot's descriptor changed while the session is live → the manager
    // converges it via PUT /api/connections/{id}/controllers/{idx}.
    void slotChanged(const QString& slotId);
    // A registered slot was detached while live → the manager removes it via
    // DELETE /api/connections/{id}/controllers/{idx}.
    void slotRemoved(int ctrlIdx);

  private:
    QString id_;
    models::DiscoveredServer server_;
    SessionState state_ = SessionState::Idle;
    std::optional<QString> connectionId_;
    std::optional<QString> boundSlotId_;
    int lastAppliedEpoch_ = -1;
    bool mouseControlGranted_ = false;

    // slotId → desired binding. A QHash would do, but std::map keeps the
    // descriptor iteration order deterministic (by slot id) for reproducible PUTs.
    std::map<QString, SlotBinding> slots_;

    ClientRef clientRef_;
    QTimer* aliveTimer_ = nullptr;
    // Driven from the main-thread alive-poll (NOT receive-thread callbacks) so
    // the session FSM + REST never run off the UDP receive thread.
    std::function<void()> onDead_;
    std::function<void(std::uint8_t reason)> onClose_;
    std::function<void()> onReconcile_;

    RumbleHandler rumbleHandler_;
    LightbarHandler lightbarHandler_;

    // Build a whole descriptor from a binding (caps folded from hasMotion /
    // hasLightbar + the analog-trigger/rumble base).
    models::ControllerDescriptor descriptorOf(const SlotBinding& b) const;
    int lowestFreeIndex() const;
    void teardownClient();
};

} // namespace dish::net
