// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "ConnectionStore.h"
#include "Models/Models.h"
#include "WifiConnectionManager.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <utility>

namespace dish::net {

// Aggregates the wifi pool into the flat [ConnectionSummary] the UI consumes
// and owns the slot->connection binding table. Mirrors
// dish-mac/Network/ConnectionHub.swift (the WiFi-only subset of the Android
// ConnectionHub.kt — no Bluetooth-HID-Device on Linux desktop).
class ConnectionHub : public QObject {
    Q_OBJECT
  public:
    using ReportSender = std::function<void(std::uint16_t, std::uint8_t, std::uint8_t, std::int16_t,
                                            std::int16_t, std::int16_t, std::int16_t)>;

    // IMU sender: gyroX/Y/Z, accelX/Y/Z, timestampDeltaUs. All host-LE.
    using MotionSender = std::function<void(std::int16_t, std::int16_t, std::int16_t, std::int16_t,
                                            std::int16_t, std::int16_t, std::uint32_t)>;

    // Battery sender: level (0..100 or 0xFF), status (BATTERY_STATUS_*).
    using BatterySender = std::function<void(std::uint8_t, std::uint8_t)>;

    // Touchpad sender: finger0(active,id,x,y), finger1(active,id,x,y), button,
    // eventTimeMs (sender uptime ms — protocol-1 MSG_TOUCHPAD trailing field).
    using TouchpadSender =
        std::function<void(bool, std::uint8_t, std::int16_t, std::int16_t, bool, std::uint8_t,
                           std::int16_t, std::int16_t, bool, std::uint32_t)>;

    ConnectionHub(WifiConnectionManager* wifi, ConnectionStore* store, QObject* parent = nullptr);

    QList<models::ConnectionSummary> connections() const { return summaries_; }
    QHash<QString, QString> bindings() const { return bindings_; }

    // Returns the live ReportSender for the connection bound to `slotId`, or
    // an empty function if nothing is bound. The returned closure does a single
    // mutex-guarded shared_ptr load on the hot path; safe to call from the SDL
    // gamepad thread.
    ReportSender reportSenderForSlot(const QString& slotId) const;

    // Parallel motion/battery routes — same shape as reportSenderForSlot,
    // returned as a closure capturing the connection pointer.
    MotionSender motionSenderForSlot(const QString& slotId) const;
    BatterySender batterySenderForSlot(const QString& slotId) const;
    TouchpadSender touchpadSenderForSlot(const QString& slotId) const;

    // Predicate answering "does the physical pad behind this slot have an
    // addressable RGB LED?". Installed by AppModel (which owns the SDL bridge
    // that detects the LED). bind() consults it so the slot's REST descriptor
    // advertises CAP_LIGHTBAR for an LED-bearing pad. When unset, slots are
    // treated as having no lightbar.
    using LightbarCapabilityFn = std::function<bool(const QString& slotId)>;
    void setLightbarCapabilityFn(LightbarCapabilityFn fn) { lightbarCapabilityFn_ = std::move(fn); }

    // Predicate answering "does the physical pad behind this slot have a
    // motion sensor (gyro/accelerometer)?". Same shape / install path as
    // LightbarCapabilityFn — bind() consults it so the REST descriptor
    // advertises CAP_MOTION per-device. Unset → treated as no motion.
    using MotionCapabilityFn = std::function<bool(const QString& slotId)>;
    void setMotionCapabilityFn(MotionCapabilityFn fn) { motionCapabilityFn_ = std::move(fn); }

    // Resolver answering "what satellite controller type is the pad behind
    // this slot?" (CONTROLLER_TYPE_XBOX / _PLAYSTATION). Installed by AppModel
    // off the SDL bridge's per-device classification; bind() threads the
    // result into the REST descriptor's `type` field so a DualSense
    // registers as a virtual DS4. Unset → CONTROLLER_TYPE_XBOX.
    using ControllerTypeFn = std::function<int(const QString& slotId)>;
    void setControllerTypeFn(ControllerTypeFn fn) { controllerTypeFn_ = std::move(fn); }

    // Resolver answering "which touchpadMode does this slot's descriptor
    // declare?" (a proto::kTouchpadMode* value). AppModel folds the full
    // ladder (pad has a touch source × the selected type's catalog gate × the
    // per-satellite pick) via reducer::resolveTouchpadMode; bind() threads the
    // result into the descriptor so DS4 touch actually forwards (a hardwired
    // "off" made the satellite discard every MSG_TOUCHPAD). Unset → off.
    using TouchpadModeFn = std::function<std::uint8_t(const QString& slotId)>;
    void setTouchpadModeFn(TouchpadModeFn fn) { touchpadModeFn_ = std::move(fn); }

    void bind(const QString& slotId, const QString& connectionId);
    void unbind(const QString& slotId);
    std::optional<models::ConnectionSummary> boundConnection(const QString& slotId) const;
    std::optional<models::ConnectionSummary> summary(const QString& id) const;

  signals:
    void changed();

  private:
    void rebuild();

    WifiConnectionManager* wifi_;
    ConnectionStore* store_;
    QList<models::ConnectionSummary> summaries_;
    QHash<QString, QString> bindings_; // slotId -> connectionId
    LightbarCapabilityFn lightbarCapabilityFn_;
    MotionCapabilityFn motionCapabilityFn_;
    ControllerTypeFn controllerTypeFn_;
    TouchpadModeFn touchpadModeFn_;
};

} // namespace dish::net
