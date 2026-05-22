// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"
#include "SatelliteClient.h"

#include <QObject>
#include <QString>
#include <QTimer>

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>

namespace dish::net {

// Internal wire-level session state for one Satellite connection. This is the
// "Presence" axis (per the shared nomenclature): how far the live network link
// has progressed for *this* connection.
//
// Distinct from the UI-facing [models::LinkState] (in Models.h), which folds
// pairing/discovery in on top of this.
//
// - Idle       — no live session (paired or not).
// - Linking    — pair+auth handshake / markConnecting is in flight; native
//                socket not yet open. UI chip: "Connecting…".
// - Live       — native socket open, heartbeat ACKs flowing. UI chip: "Online".
// - Faltering  — Live, but the heartbeat-miss counter is non-zero and below
//                the death threshold. UI chip: "Unsteady". **Not yet entered**
//                — reaching it requires the native side to expose the
//                consecutive-missed count separately from the binary
//                isAlive() boolean. Today the alive-poll flips Live → Idle
//                directly when misses hit the threshold.
// - Stale      — the session collapsed but we still hold a shared key for the
//                server. Used by the silent-recovery path (alive-poll's onDead
//                callback) so the UI chip reads "Needs pairing" rather than
//                "Offline" while WifiConnectionManager retries the handshake
//                in the background. Mirrors dish-android's `staleSatelliteIds`
//                marker (see SatelliteConnectionManager.kt:128). Cleared the
//                moment a silent retry lands a Live session, or the user
//                explicitly drives a fresh pair from the UI.
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

// A single live or potential WiFi session to one Satellite server. Mirrors
// dish-mac/Network/WifiConnection.swift.
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

    void updateServer(const models::DiscoveredServer& s);
    void markConnecting();
    void markConnected(std::shared_ptr<SatelliteClient> client, const QString& connectionId,
                       std::function<void()> onDead);
    void markDisconnected();
    // Like markDisconnected, but lands in SessionState::Stale instead of Idle.
    // Used by the silent-recovery path: a dropped session whose shared key is
    // still locally valid is "Needs pairing" to the UI (chip cue), not
    // "Offline", until either the silent retry succeeds (back to Live) or the
    // user takes an explicit action.
    void markStale();

    // Bind this connection to a controller slot. `controllerType` is the
    // satellite virtual-device type (CONTROLLER_TYPE_XBOX / _PLAYSTATION).
    // `hasLightbar` is true when the bound physical pad exposes an addressable
    // RGB LED — it gates the CAP_LIGHTBAR (0x0008) bit. `hasMotion` is true
    // when the pad exposes an IMU — it gates the CAP_MOTION (0x0004) bit the
    // same per-device way. All three are stored so a later registration (on
    // reconnect) advertises the same type / capabilities.
    void attachSlot(const QString& slotId, int controllerType, bool hasLightbar, bool hasMotion);
    void detachSlot();

    // True between a controllerAdd send and the matching ACK / timeout. Drives
    // the dashboard's "registering" spinner.
    bool isRegisteringController() const { return controllerRegistering_; }

    // Hot path: called directly from the SDL gamepad thread.
    void sendReport(std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt, std::int16_t lx,
                    std::int16_t ly, std::int16_t rx, std::int16_t ry);

    // Hot path: forward an IMU sample to the satellite. Called from the
    // GamepadInputProcessor's motion publish path on the SDL sensor thread.
    void sendMotion(std::int16_t gyroX, std::int16_t gyroY, std::int16_t gyroZ, std::int16_t accelX,
                    std::int16_t accelY, std::int16_t accelZ, std::uint32_t timestampDeltaUs);

    // Forward a battery sample to the satellite. Called from the battery-poll
    // path on the SDL gamepad thread (30 s default cadence).
    void sendBattery(std::uint8_t level, std::uint8_t status);

    // Forward a touchpad sample to the satellite. Called from the SDL
    // touchpad-event path. Up to two fingers + the clickable-pad button.
    void sendTouchpad(bool finger0Active, std::uint8_t finger0Id, std::int16_t finger0X,
                      std::int16_t finger0Y, bool finger1Active, std::uint8_t finger1Id,
                      std::int16_t finger1X, std::int16_t finger1Y, bool buttonPressed);

    // Install the per-connection rumble handler. The handler is invoked from
    // the SatelliteClient's receive thread on every MSG_RUMBLE we decode.
    // Stored on the WifiConnection (not the per-session SatelliteClient) so
    // it survives reconnects: markConnected() re-installs it on the new
    // client instance.
    using RumbleHandler = std::function<void(const SatelliteClient::RumbleMessage&)>;
    void setRumbleHandler(RumbleHandler handler);

    // Same pattern as setRumbleHandler but for MSG_LIGHTBAR (Task 1.4).
    using LightbarHandler = std::function<void(const SatelliteClient::LightbarMessage&)>;
    void setLightbarHandler(LightbarHandler handler);

  signals:
    void changed();
    // Transient one-shot error — a controller registration was rejected or
    // timed out. The manager fans this out to AppModel's errorMessage toast.
    void errorOccurred(const QString& message);
    // Emitted when the in-flight controller registration for `slotId` was
    // rejected or timed out. Listened to by ConnectionHub to roll back the
    // local binding so the UI reflects reality.
    void registrationFailed(const QString& slotId);
    // Transient one-shot WARNING — the controller registered successfully but
    // the receiver's motion-flags byte indicates motion bytes won't reach the
    // game. Surfaced when CAP_MOTION was advertised AND the satellite reported
    // backendOk == false (kernel rejected the IMU sink) or
    // sinkSupportedForType == false (the receiver backend has no IMU surface
    // for this controller's chosen type — e.g. an Xbox virtual pad on a
    // ViGEm/uinput backend). Unlike errorOccurred this does NOT roll the
    // binding back: the controller is still usable, motion bytes are just
    // dropped at the receiver. The manager forwards it as a Warn notification.
    void motionDeliveryWarning(const QString& message);

  private:
    static constexpr int kDefaultCtrlIndex = 0;
    // Base capability word advertised in MSG_CONTROLLER_ADD: analog triggers
    // (0x0001) | rumble (0x0002). CAP_MOTION (0x0004) and CAP_LIGHTBAR
    // (0x0008) are NOT in here — they are per-controller (only pads with the
    // matching hardware) and OR-ed in by registerController from the bound
    // slot's capabilities. See SatelliteClient::kCap* mirrors.
    static constexpr std::uint16_t kDefaultCaps =
        SatelliteClient::kCapAnalogTriggers | SatelliteClient::kCapRumble; // 0x0003
    // The non-blocking ACK poll: `kAckWaitAttempts` ticks of `kAckWaitIntervalMs`
    // each (≈2 s total) before the registration is treated as timed-out.
    static constexpr int kAckWaitAttempts = 20;
    static constexpr int kAckWaitIntervalMs = 100;

    void registerController(int type);
    // QTimer-driven poll of the SatelliteClient ACK state — runs on the Qt
    // main thread so the UI never blocks waiting for the server. On ACK_OK it
    // sends MSG_CONTROLLER_TYPE and marks the controller added; on an error
    // code or timeout it emits errorOccurred + registrationFailed.
    void pollControllerAck();
    // Stop the ACK poll timer and clear the "registering" flag, emitting
    // changed() so the spinner updates.
    void finishRegistration();
    // Composed capability word the registration / caps-update path advertises.
    // Folds kDefaultCaps (analog triggers | rumble) with the per-slot CAP_MOTION
    // (0x0004) and CAP_LIGHTBAR (0x0008) hardware bits. Single source of truth
    // shared between registerController and any future mid-session
    // sendCapsUpdate caller, so the two paths can't drift.
    std::uint16_t composedCaps() const;

    QString id_;
    models::DiscoveredServer server_;
    SessionState state_ = SessionState::Idle;
    std::optional<QString> connectionId_;
    std::optional<QString> boundSlotId_;

    ClientRef clientRef_;
    QTimer* aliveTimer_ = nullptr;
    // ACK poll timer for an in-flight controller registration. Created lazily
    // on the first registerController and reused thereafter.
    QTimer* ackPollTimer_ = nullptr;
    int ackPollCount_ = 0;
    bool controllerRegistering_ = false;
    std::function<void()> onDead_;
    bool controllerAdded_ = false;
    int pendingControllerType_ = 0;
    // Whether the bound slot's physical pad has an addressable RGB LED. Set by
    // attachSlot; consumed by registerController to advertise CAP_LIGHTBAR.
    bool lightbarCapable_ = false;
    // Whether the bound slot's physical pad has a motion sensor (gyro/accel).
    // Set by attachSlot; consumed by registerController to advertise
    // CAP_MOTION per-device — an Xbox pad never advertises it.
    bool motionCapable_ = false;
    // The most recent capability word the dish has advertised to the receiver
    // for this connection's controller, or std::nullopt before the first
    // registration ACK. Used by a future mid-session toggle to de-dup
    // MSG_CONTROLLER_CAPS_UPDATE packets — if composedCaps() matches
    // lastAdvertisedCaps_, there is nothing to send. Cleared on disconnect /
    // detach so a fresh registration always re-establishes the value.
    // Mirrors dish-android's SlotBinding.lastAdvertisedCaps.
    std::optional<std::uint16_t> lastAdvertisedCaps_;

    // Set once during composition; re-applied to each fresh SatelliteClient
    // in markConnected() so we don't lose rumble across reconnects.
    RumbleHandler rumbleHandler_;
    // Same lifecycle as rumbleHandler_, for the dedicated lightbar stream.
    LightbarHandler lightbarHandler_;
};

} // namespace dish::net
