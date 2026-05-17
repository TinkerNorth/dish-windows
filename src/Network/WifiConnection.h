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

enum class WifiState { Idle, Connecting, Connected };

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
    WifiState state() const { return state_; }
    std::optional<QString> connectionId() const { return connectionId_; }
    std::optional<QString> boundSlotId() const { return boundSlotId_; }
    std::shared_ptr<SatelliteClient> client() const { return clientRef_.get(); }

    void updateServer(const models::DiscoveredServer& s);
    void markConnecting();
    void markConnected(std::shared_ptr<SatelliteClient> client, const QString& connectionId,
                       std::function<void()> onDead);
    void markDisconnected();

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

    QString id_;
    models::DiscoveredServer server_;
    WifiState state_ = WifiState::Idle;
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

    // Set once during composition; re-applied to each fresh SatelliteClient
    // in markConnected() so we don't lose rumble across reconnects.
    RumbleHandler rumbleHandler_;
    // Same lifecycle as rumbleHandler_, for the dedicated lightbar stream.
    LightbarHandler lightbarHandler_;
};

} // namespace dish::net
