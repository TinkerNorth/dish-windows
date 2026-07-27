// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WifiConnection.h"

#include "core/reducer/Reconcile.h"

#include <cmath>

namespace dish::net {

namespace {

// Base capability word every descriptor advertises: analog triggers + rumble.
// CAP_MOTION / CAP_LIGHTBAR are per-controller (only pads with the matching
// hardware) and folded in from the slot's capabilities.
constexpr std::uint16_t kBaseCaps =
    SatelliteClient::kCapAnalogTriggers | SatelliteClient::kCapRumble;

} // namespace

WifiConnection::WifiConnection(QString id, models::DiscoveredServer server, QObject* parent)
    : QObject(parent), id_(std::move(id)), server_(std::move(server)) {}

WifiConnection::~WifiConnection() { markDisconnected(); }

void WifiConnection::updateServer(const models::DiscoveredServer& s) {
    server_ = s;
    emit changed();
}

void WifiConnection::markConnecting() {
    if (state_ == SessionState::Live) { return; }
    state_ = SessionState::Linking;
    emit changed();
}

models::ControllerDescriptor WifiConnection::descriptorOf(const SlotBinding& b) const {
    models::ControllerDescriptor d;
    d.ctrlIdx = b.controllerIndex;
    d.type = static_cast<std::uint8_t>(b.controllerType);
    d.caps = SatelliteClient::withLightbarCapability(
        SatelliteClient::withMotionCapability(kBaseCaps, b.hasMotion), b.hasLightbar);
    d.touchpadMode = b.touchpadMode;
    return d;
}

int WifiConnection::lowestFreeIndex() const {
    int i = 0;
    for (;;) {
        bool taken = false;
        for (const auto& [slotId, b] : slots_) {
            if (b.controllerIndex == i) {
                taken = true;
                break;
            }
        }
        if (!taken) { return i; }
        ++i;
    }
}

void WifiConnection::teardownClient() {
    if (aliveTimer_ != nullptr) {
        aliveTimer_->stop();
        aliveTimer_->deleteLater();
        aliveTimer_ = nullptr;
    }
    if (auto existing = clientRef_.get()) {
        existing->stopHeartbeat();
        existing->stopReceiveLoop();
        existing->closeSocket();
    }
    clientRef_.set(nullptr);
    connectionId_.reset();
    lastAppliedEpoch_ = -1;
    mouseControlGranted_ = false;
    // The latency readout is session-scoped — zero it so the row can't show a
    // stale figure across a reconnect. Every teardown path emits changed()
    // right after, so no separate telemetryChanged is needed.
    latencyOneWayMs_ = 0.0;
    latencySamples_ = 0;
    rekeyRequested_ = false;
    // A dropped session leaves no virtual pads applied — clear the registered
    // flags so streams gate off until the next PUT re-applies them.
    for (auto& [slotId, b] : slots_) { b.registered = false; }
}

void WifiConnection::markConnected(const std::shared_ptr<SatelliteClient>& client,
                                   const QString& connectionId, int epoch, bool mouseControlGranted,
                                   std::function<void()> onDead,
                                   std::function<void(std::uint8_t)> onClose,
                                   std::function<void()> onReconcile,
                                   std::function<void()> onRekey) {
    if (state_ != SessionState::Linking) { return; }
    clientRef_.set(client);
    connectionId_ = connectionId;
    lastAppliedEpoch_ = epoch;
    mouseControlGranted_ = mouseControlGranted;
    state_ = SessionState::Live;
    onDead_ = std::move(onDead);
    onClose_ = std::move(onClose);
    onReconcile_ = std::move(onReconcile);
    onRekey_ = std::move(onRekey);
    rekeyRequested_ = false;

    if (rumbleHandler_) { client->setRumbleHandler(rumbleHandler_); }
    if (lightbarHandler_) { client->setLightbarHandler(lightbarHandler_); }
    // Rumble/lightbar fire on the receive thread (they only hand off to the SDL
    // bridge via its own queue). Close-notify and the enriched-ack reconcile,
    // however, drive the session FSM + REST — so they are NOT receive-thread
    // callbacks; the main-thread alive-poll below reads the client's
    // thread-safe atomics instead. Mirrors dish-android's aliveJob (it polls
    // getSessionCloseReason / getServerEpoch rather than taking a push).
    client->startReceiveLoop();
    client->startHeartbeat();

    if (aliveTimer_ != nullptr) {
        aliveTimer_->stop();
        aliveTimer_->deleteLater();
    }
    aliveTimer_ = new QTimer(this);
    aliveTimer_->setInterval(1000);
    QObject::connect(aliveTimer_, &QTimer::timeout, this, &WifiConnection::onAliveTick);
    aliveTimer_->start();
    emit changed();
}

void WifiConnection::onAliveTick() {
    const auto c = clientRef_.get();
    if (!c) { return; }
    // Refresh the latency readout each tick, rounded to the 0.1 ms display
    // precision so sub-jitter median moves don't re-emit. telemetryChanged
    // (not changed) keeps the 1 Hz tick off the rebuild cascade.
    const auto latency = c->latencySnapshot();
    const double rounded = latency.samples > 0 ? std::lround(latency.oneWayMs * 10.0) / 10.0 : 0.0;
    if (rounded != latencyOneWayMs_ || latency.samples != latencySamples_) {
        latencyOneWayMs_ = rounded;
        latencySamples_ = latency.samples;
        emit telemetryChanged();
    }
    // An authenticated close-notify is terminal NOW: the session is already
    // gone server-side, so don't wait out the heartbeat death window.
    const std::int32_t closeReason = c->sessionCloseReason();
    if (closeReason >= 0) {
        const auto cb = onClose_;
        if (cb) { cb(static_cast<std::uint8_t>(closeReason)); }
        return;
    }
    if (!c->isAlive()) {
        const auto cb = onDead_;
        if (cb) { cb(); }
        return;
    }
    // Alive but faltering: two consecutive missed acks is the contract's
    // "not responding" display threshold — the chip reads "Unstable" for the
    // ~6 s before the death threshold instead of a confident "Online", and
    // recovers to Live the moment an ack lands. Only flips between the two
    // steady states; Linking/Stale keep their own owners.
    const bool faltering = c->missedAcks() >= SatelliteClient::kHeartbeatMissNotResponding;
    const SessionState steady = faltering ? SessionState::Faltering : SessionState::Live;
    if ((state_ == SessionState::Live || state_ == SessionState::Faltering) && state_ != steady) {
        state_ = steady;
        emit changed();
    }
    // Alive: nudge the reconcile (the manager re-checks epoch/bitmap drift
    // against applied and only does the GET-then-rePUT when it actually
    // diverged).
    const auto cb = onReconcile_;
    if (cb) { cb(); }
    // Proactive re-key before the send counter can exhaust (contract §Crypto:
    // re-PUT past 0xF0000000). A session that exhausts anyway goes silent in
    // SatelliteClient and heals via the death-retry re-PUT.
    if (reducer::counterNeedsRepush(c->sendCounter())) {
        if (!rekeyRequested_ && onRekey_) {
            rekeyRequested_ = true;
            onRekey_();
        }
    } else {
        rekeyRequested_ = false;
    }
}

void WifiConnection::markDisconnected() {
    auto existing = clientRef_.get();
    if (state_ == SessionState::Idle && !existing) { return; }
    teardownClient();
    state_ = SessionState::Idle;
    emit changed();
}

void WifiConnection::markStale() {
    auto existing = clientRef_.get();
    if (state_ == SessionState::Stale && !existing) { return; }
    teardownClient();
    state_ = SessionState::Stale;
    emit changed();
}

void WifiConnection::attachSlot(const QString& slotId, int controllerType, bool hasLightbar,
                                bool hasMotion) {
    auto it = slots_.find(slotId);
    if (it == slots_.end()) {
        SlotBinding b;
        b.controllerIndex = lowestFreeIndex();
        b.controllerType = controllerType;
        b.hasLightbar = hasLightbar;
        b.hasMotion = hasMotion;
        b.registered = false;
        slots_.emplace(slotId, b);
        boundSlotId_ = slotId;
        if (state_ == SessionState::Live) { emit slotChanged(slotId); }
    } else {
        // Re-declare: the WHOLE descriptor in one shot.
        const bool changed = it->second.controllerType != controllerType ||
                             it->second.hasLightbar != hasLightbar ||
                             it->second.hasMotion != hasMotion;
        it->second.controllerType = controllerType;
        it->second.hasLightbar = hasLightbar;
        it->second.hasMotion = hasMotion;
        if (changed && state_ == SessionState::Live) { emit slotChanged(slotId); }
    }
    emit changed();
}

void WifiConnection::detachSlot() {
    if (!boundSlotId_.has_value()) { return; }
    detachSlot(*boundSlotId_);
}

void WifiConnection::detachSlot(const QString& slotId) {
    const auto it = slots_.find(slotId);
    if (it == slots_.end()) { return; }
    const SlotBinding removed = it->second;
    slots_.erase(it);
    if (boundSlotId_.has_value() && *boundSlotId_ == slotId) { boundSlotId_.reset(); }
    // Removing a registered slot while live → DELETE the controller (the
    // session lives on; zero-controller sessions are valid).
    if (state_ == SessionState::Live && removed.registered) {
        emit slotRemoved(removed.controllerIndex);
    }
    emit changed();
}

std::optional<models::ControllerDescriptor>
WifiConnection::descriptorFor(const QString& slotId) const {
    const auto it = slots_.find(slotId);
    if (it == slots_.end()) { return std::nullopt; }
    return descriptorOf(it->second);
}

QList<models::ControllerDescriptor> WifiConnection::desiredDescriptors() const {
    QList<models::ControllerDescriptor> out;
    for (const auto& [slotId, b] : slots_) { out.append(descriptorOf(b)); }
    return out;
}

QString WifiConnection::slotIdForIndex(int ctrlIdx) const {
    for (const auto& [slotId, b] : slots_) {
        if (b.controllerIndex == ctrlIdx) { return slotId; }
    }
    return {};
}

bool WifiConnection::wantsMouseControl() const {
    for (const auto& [slotId, b] : slots_) {
        if (b.touchpadMode == proto::kTouchpadModeMouse) { return true; }
    }
    return false;
}

void WifiConnection::applyResults(const QList<models::ControllerApplyDto>& results) {
    if (results.isEmpty()) { return; }
    std::map<int, const models::ControllerApplyDto*> byIdx;
    for (const auto& r : results) { byIdx[r.ctrlIdx] = &r; }
    QList<models::ControllerApplyDto> failures;
    for (auto& [slotId, b] : slots_) {
        const auto it = byIdx.find(b.controllerIndex);
        if (it == byIdx.end()) { continue; }
        const auto& r = *it->second;
        // A failed replug keeps the PREVIOUS pad alive (appliedType reports it);
        // streams keep flowing rather than killing a working pad over a type the
        // driver couldn't switch.
        b.registered = r.slotIsLive();
        if (!r.ok()) { failures.append(r); }
    }
    emit changed();
    for (const auto& f : failures) {
        emit errorOccurred(QStringLiteral("Controller #%1: %2").arg(f.ctrlIdx).arg(f.result));
    }
}

std::uint16_t WifiConnection::registeredBitmap() const {
    std::vector<reducer::DesiredSlot> reg;
    for (const auto& [slotId, b] : slots_) {
        if (b.registered) {
            reg.push_back({static_cast<std::uint8_t>(b.controllerIndex),
                           static_cast<std::uint8_t>(b.controllerType)});
        }
    }
    return reducer::expectedBitmap(reg);
}

bool WifiConnection::matchesAppliedView(const models::SessionViewDto& view) const {
    std::vector<reducer::DesiredSlot> desired;
    desired.reserve(slots_.size());
    for (const auto& [slotId, b] : slots_) {
        desired.push_back({static_cast<std::uint8_t>(b.controllerIndex),
                           static_cast<std::uint8_t>(b.controllerType)});
    }
    std::vector<reducer::AppliedSlot> applied;
    for (const auto& c : view.controllers) {
        applied.push_back({static_cast<std::uint8_t>(c.ctrlIdx),
                           static_cast<std::uint8_t>(c.appliedType), c.active});
    }
    // The host-feature grant is applied state too: a slot toggled to mouse
    // mid-session leaves wants≠granted (the grant is only computed at session
    // PUT), so the converge re-PUT is what heals it.
    const bool mouseMatch = view.mouseControl.granted == wantsMouseControl();
    return reducer::appliedMatchesDesired(desired, applied, mouseMatch);
}

void WifiConnection::sendReport(std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt,
                                std::int16_t lx, std::int16_t ly, std::int16_t rx,
                                std::int16_t ry) {
    if (!boundSlotId_.has_value()) { return; }
    const auto it = slots_.find(*boundSlotId_);
    if (it == slots_.end() || !it->second.registered) { return; }
    if (auto c = clientRef_.get()) {
        c->sendReport(it->second.controllerIndex, buttons, lt, rt, lx, ly, rx, ry);
    }
}

void WifiConnection::sendMotion(std::int16_t gyroX, std::int16_t gyroY, std::int16_t gyroZ,
                                std::int16_t accelX, std::int16_t accelY, std::int16_t accelZ,
                                std::uint32_t timestampDeltaUs) {
    if (!boundSlotId_.has_value()) { return; }
    const auto it = slots_.find(*boundSlotId_);
    if (it == slots_.end() || !it->second.registered) { return; }
    if (auto c = clientRef_.get()) {
        c->sendMotion(it->second.controllerIndex, gyroX, gyroY, gyroZ, accelX, accelY, accelZ,
                      timestampDeltaUs);
    }
}

void WifiConnection::sendBattery(std::uint8_t level, std::uint8_t status) {
    if (!boundSlotId_.has_value()) { return; }
    const auto it = slots_.find(*boundSlotId_);
    if (it == slots_.end() || !it->second.registered) { return; }
    if (auto c = clientRef_.get()) { c->sendBattery(it->second.controllerIndex, level, status); }
}

void WifiConnection::sendTouchpad(bool finger0Active, std::uint8_t finger0Id, std::int16_t finger0X,
                                  std::int16_t finger0Y, bool finger1Active, std::uint8_t finger1Id,
                                  std::int16_t finger1X, std::int16_t finger1Y, bool buttonPressed,
                                  std::uint32_t eventTimeMs) {
    if (!boundSlotId_.has_value()) { return; }
    const auto it = slots_.find(*boundSlotId_);
    if (it == slots_.end() || !it->second.registered) { return; }
    if (auto c = clientRef_.get()) {
        c->sendTouchpad(it->second.controllerIndex, finger0Active, finger0Id, finger0X, finger0Y,
                        finger1Active, finger1Id, finger1X, finger1Y, buttonPressed, eventTimeMs);
    }
}

void WifiConnection::setRumbleHandler(RumbleHandler handler) {
    rumbleHandler_ = std::move(handler);
    if (auto c = clientRef_.get()) { c->setRumbleHandler(rumbleHandler_); }
}

void WifiConnection::setLightbarHandler(LightbarHandler handler) {
    lightbarHandler_ = std::move(handler);
    if (auto c = clientRef_.get()) { c->setLightbarHandler(lightbarHandler_); }
}

} // namespace dish::net
