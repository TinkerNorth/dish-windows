// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnection.h"

#include <QThread>

namespace dish::net {

WifiConnection::WifiConnection(QString id, models::DiscoveredServer server, QObject* parent)
    : QObject(parent), id_(std::move(id)), server_(std::move(server)) {}

WifiConnection::~WifiConnection() { markDisconnected(); }

void WifiConnection::updateServer(const models::DiscoveredServer& s) {
    server_ = s;
    emit changed();
}

void WifiConnection::markConnecting() {
    if (state_ == WifiState::Connected) { return; }
    state_ = WifiState::Connecting;
    emit changed();
}

void WifiConnection::markConnected(std::shared_ptr<SatelliteClient> client,
                                   const QString& connectionId, std::function<void()> onDead) {
    if (state_ != WifiState::Connecting) { return; }
    clientRef_.set(client);
    connectionId_ = connectionId;
    state_ = WifiState::Connected;
    onDead_ = std::move(onDead);

    client->resetControllerAck();
    if (rumbleHandler_) { client->setRumbleHandler(rumbleHandler_); }
    if (lightbarHandler_) { client->setLightbarHandler(lightbarHandler_); }
    client->startReceiveLoop();
    client->startHeartbeat();

    if (aliveTimer_ != nullptr) {
        aliveTimer_->stop();
        aliveTimer_->deleteLater();
    }
    aliveTimer_ = new QTimer(this);
    aliveTimer_->setInterval(1000);
    QObject::connect(aliveTimer_, &QTimer::timeout, this, [this] {
        const auto c = clientRef_.get();
        if (!c || !c->isAlive()) {
            const auto cb = onDead_;
            if (cb) { cb(); }
        }
    });
    aliveTimer_->start();
    emit changed();

    if (boundSlotId_.has_value() && !controllerAdded_) {
        registerController(pendingControllerType_);
    }
}

void WifiConnection::markDisconnected() {
    auto existing = clientRef_.get();
    if (state_ == WifiState::Idle && !existing) { return; }
    if (aliveTimer_ != nullptr) {
        aliveTimer_->stop();
        aliveTimer_->deleteLater();
        aliveTimer_ = nullptr;
    }
    if (existing) {
        existing->stopHeartbeat();
        existing->stopReceiveLoop();
        existing->closeSocket();
    }
    clientRef_.set(nullptr);
    connectionId_.reset();
    controllerAdded_ = false;
    state_ = WifiState::Idle;
    emit changed();
}

void WifiConnection::attachSlot(const QString& slotId, int controllerType) {
    boundSlotId_ = slotId;
    pendingControllerType_ = controllerType;
    if (state_ == WifiState::Connected && !controllerAdded_) { registerController(controllerType); }
    emit changed();
}

void WifiConnection::detachSlot() {
    if (!boundSlotId_.has_value()) { return; }
    boundSlotId_.reset();
    if (controllerAdded_) {
        if (auto c = clientRef_.get()) { c->controllerRemove(kDefaultCtrlIndex); }
    }
    controllerAdded_ = false;
    emit changed();
}

void WifiConnection::registerController(int type) {
    auto c = clientRef_.get();
    if (!c) { return; }
    c->resetControllerAck();
    c->controllerAdd(kDefaultCtrlIndex, kDefaultCaps);
    // Spin briefly waiting for the server's controller ACK; same shape as the
    // Mac client. This blocks the calling (main) thread for up to ~2s in the
    // worst case, but the satellite normally responds within a few ms.
    for (int i = 0; i < kAckWaitAttempts && c->lastControllerAck() == -1; ++i) {
        QThread::msleep(kAckWaitIntervalMs);
    }
    if (c->lastControllerAck() != -1) {
        c->sendControllerType(kDefaultCtrlIndex, type);
        controllerAdded_ = true;
    }
}

void WifiConnection::sendReport(std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt,
                                std::int16_t lx, std::int16_t ly, std::int16_t rx,
                                std::int16_t ry) {
    if (auto c = clientRef_.get()) {
        c->sendReport(kDefaultCtrlIndex, buttons, lt, rt, lx, ly, rx, ry);
    }
}

void WifiConnection::sendMotion(std::int16_t gyroX, std::int16_t gyroY, std::int16_t gyroZ,
                                std::int16_t accelX, std::int16_t accelY, std::int16_t accelZ,
                                std::uint32_t timestampDeltaUs) {
    if (auto c = clientRef_.get()) {
        c->sendMotion(kDefaultCtrlIndex, gyroX, gyroY, gyroZ, accelX, accelY, accelZ,
                      timestampDeltaUs);
    }
}

void WifiConnection::sendBattery(std::uint8_t level, std::uint8_t status) {
    if (auto c = clientRef_.get()) { c->sendBattery(kDefaultCtrlIndex, level, status); }
}

void WifiConnection::sendTouchpad(bool finger0Active, std::uint8_t finger0Id, std::int16_t finger0X,
                                  std::int16_t finger0Y, bool finger1Active, std::uint8_t finger1Id,
                                  std::int16_t finger1X, std::int16_t finger1Y,
                                  bool buttonPressed) {
    if (auto c = clientRef_.get()) {
        c->sendTouchpad(kDefaultCtrlIndex, finger0Active, finger0Id, finger0X, finger0Y,
                        finger1Active, finger1Id, finger1X, finger1Y, buttonPressed);
    }
}

void WifiConnection::setRumbleHandler(RumbleHandler handler) {
    rumbleHandler_ = std::move(handler);
    // Apply immediately if a session is already live; otherwise markConnected
    // will pick up the new handler the next time it runs.
    if (auto c = clientRef_.get()) { c->setRumbleHandler(rumbleHandler_); }
}

void WifiConnection::setLightbarHandler(LightbarHandler handler) {
    lightbarHandler_ = std::move(handler);
    if (auto c = clientRef_.get()) { c->setLightbarHandler(lightbarHandler_); }
}

} // namespace dish::net
