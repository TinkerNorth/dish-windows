// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "AppModel.h"

#include "LightbarRouting.h"
#include "core/reducer/RumbleRouting.h"
#include "core/reducer/TouchpadRouting.h"

#include <QLocale>

#include <chrono>
#include <vector>

namespace dish {

AppModel::AppModel(QObject* parent)
    : AppModel(std::make_unique<util::SetThreadExecutionStateInhibitor>(), parent) {}

AppModel::AppModel(std::unique_ptr<util::DisplaySleepInhibitor> inhibitor, QObject* parent)
    : QObject(parent), store_(std::make_unique<net::ConnectionStore>()),
      wifi_(new net::WifiConnectionManager(store_.get(), this)),
      hub_(new net::ConnectionHub(wifi_, store_.get(), this)),
      connections_(new composer::ConnectionCoordinator(wifi_, hub_, this)),
      bridge_(new input::SDLGamepadBridge(&processor_, this)),
      featureSettings_(new FeatureSettings(this)), autoReconnectTimer_(new QTimer(this)),
      inhibitor_(std::move(inhibitor)), wake_(inhibitor_.get()),
      catalogHttp_(new net::HTTPClient(this)), catalogRepo_(catalogHttp_),
      catalogSnapshot_(composer::CatalogSnapshot{}), catalogComposer_(catalogSnapshot_),
      motionEnabledStore_(&motionPrefRepo_) {
    QObject::connect(hub_, &net::ConnectionHub::changed, this, &AppModel::onHubChanged);
    QObject::connect(bridge_, &input::SDLGamepadBridge::devicesChanged, this,
                     &AppModel::onBridgeDevicesChanged);
    QObject::connect(wifi_, &net::WifiConnectionManager::connectionEvent, this,
                     &AppModel::onWifiEvent);
    // poolChanged fires every time a WifiConnection is created or transitions
    // state — perfect place to make sure new connections have a rumble
    // handler. Idempotent on already-wired connections.
    QObject::connect(wifi_, &net::WifiConnectionManager::poolChanged, this,
                     &AppModel::installRumbleHandlers);

    autoReconnectTimer_->setInterval(15'000);
    QObject::connect(autoReconnectTimer_, &QTimer::timeout, this,
                     [this] { wifi_->autoReconnectAll(); });

    // Hot-path callback. Looks up routing[deviceId] under a short-held mutex
    // and forwards directly. Called on the SDL gamepad thread.
    processor_.setReportSender([this](const std::string& did, std::uint16_t buttons,
                                      std::uint8_t lt, std::uint8_t rt, std::int16_t lx,
                                      std::int16_t ly, std::int16_t rx, std::int16_t ry) {
        net::ConnectionHub::ReportSender sender;
        {
            std::lock_guard<std::mutex> lock(routingMtx_);
            sender = routing_.value(QString::fromStdString(did));
        }
        if (sender) { sender(buttons, lt, rt, lx, ly, rx, ry); }
    });

    processor_.setMotionSender([this](const std::string& did, std::int16_t gx, std::int16_t gy,
                                      std::int16_t gz, std::int16_t ax, std::int16_t ay,
                                      std::int16_t az, std::uint32_t dtUs) {
        net::ConnectionHub::MotionSender sender;
        {
            std::lock_guard<std::mutex> lock(routingMtx_);
            sender = motionRouting_.value(QString::fromStdString(did));
        }
        if (sender) { sender(gx, gy, gz, ax, ay, az, dtUs); }
    });

    processor_.setBatterySender(
        [this](const std::string& did, std::uint8_t level, std::uint8_t status) {
            net::ConnectionHub::BatterySender sender;
            {
                std::lock_guard<std::mutex> lock(routingMtx_);
                sender = batteryRouting_.value(QString::fromStdString(did));
            }
            if (sender) { sender(level, status); }
        });

    processor_.setTouchpadSender([this](const std::string& did,
                                        const input::GamepadInputProcessor::TouchpadSample& s) {
        net::ConnectionHub::TouchpadSender sender;
        {
            std::lock_guard<std::mutex> lock(routingMtx_);
            sender = touchpadRouting_.value(QString::fromStdString(did));
        }
        if (sender) {
            // protocol-1 MSG_TOUCHPAD carries a sender-side uptime-ms
            // timestamp (mouse-mode timing scales by the delta between
            // consecutive samples). The SDL touchpad path forwards every
            // assembled state change with no resends, so a fresh monotonic
            // stamp per publish matches the contract's eventTimeMs. The pure
            // reducer::assembleTouchpadForward threads eventTimeMs end-to-end
            // (the 2e routing fix); the wire encoder Wave 1 extended to 16 bytes
            // reads it back. mouseControl stays false for v1 (D2; sent by 2b).
            const auto nowMs =
                static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                               std::chrono::steady_clock::now().time_since_epoch())
                                               .count());
            const auto fwd = reducer::assembleTouchpadForward(
                s.finger0Active, s.finger0Id, s.finger0X, s.finger0Y, s.finger1Active, s.finger1Id,
                s.finger1X, s.finger1Y, s.buttonPressed, nowMs);
            sender(fwd.finger0Active, fwd.finger0Id, fwd.finger0X, fwd.finger0Y, fwd.finger1Active,
                   fwd.finger1Id, fwd.finger1X, fwd.finger1Y, fwd.buttonPressed, fwd.eventTimeMs);
        }
    });

    // Teach the hub how to look up a slot's lightbar capability so a bind()
    // can advertise CAP_LIGHTBAR. The slot id is the SDL bridge device id, so
    // the lookup is a scan of bridge devices for the matching LED flag.
    hub_->setLightbarCapabilityFn([this](const QString& slotId) {
        for (const auto& d : bridge_->devices()) {
            if (d.id == slotId) { return d.hasLightbar; }
        }
        return false;
    });

    // Same lookup for the per-device motion capability — gates CAP_MOTION in
    // the controller-add so an Xbox pad never advertises it. Workstream 2d folds
    // the user's per-slot motion toggle into the negotiation: CAP_MOTION is sent
    // iff the pad HAS a gyro AND the user left motion enabled — the same
    // `hasGyro ∧ userEnabled` rule MotionCapability::toCapBits derives.
    hub_->setMotionCapabilityFn([this](const QString& slotId) {
        bool hasGyro = false;
        for (const auto& d : bridge_->devices()) {
            if (d.id == slotId) {
                hasGyro = d.motionCapable;
                break;
            }
        }
        return hasGyro && motionEnabledStore_.isEnabled(slotId.toStdString());
    });

    // And the controller type (Xbox / PlayStation) so the controller-add can
    // send the right MSG_CONTROLLER_TYPE hint — a DualSense → virtual DS4. The
    // user's Emulate override (ControllerTypeStore, Workstream 2c) wins over the
    // SDL hardware classification; resolveControllerType applies that ladder so
    // the choice is threaded into the descriptor PUT.
    hub_->setControllerTypeFn(
        [this](const QString& slotId) { return resolveControllerType(slotId); });

    rebuild();
}

AppModel::~AppModel() { bridge_->stop(); }

void AppModel::installRumbleHandlers() {
    for (auto* conn : wifi_->connections()) {
        const QString id = conn->id();
        if (rumbleWiredConnections_.contains(id)) { continue; }
        rumbleWiredConnections_.insert(id);
        // Capture `this` and the connection id by value. The handler runs on
        // the SatelliteClient receive thread; it only reads structures
        // protected by their own locks (hub bindings, bridge device map).
        conn->setRumbleHandler([this, id](const net::SatelliteClient::RumbleMessage& rm) {
            // Snapshot the live connection→slot bindings ONCE off the receive
            // thread, then decide with the pure reducer::resolveRumble (the
            // android RumbleRouter pattern: "read each StateFlow once into an
            // immutable snapshot, decide via a pure function"). For dish-windows
            // slot.id == the SDL bridge device id, so the resolved target IS the
            // device id to actuate. The Phone / DirectUsb arms are dropped
            // (physical-only; USB-direct is 2g).
            const auto bindings = hub_->bindings();
            std::vector<reducer::RumbleConnectionSnapshot> snapshot;
            for (auto* c : wifi_->connections()) {
                reducer::RumbleConnectionSnapshot s;
                s.connId = c->id();
                s.connected = c->state() == net::SessionState::Live;
                for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
                    if (it.value() == s.connId) {
                        s.boundDeviceId = it.key();
                        break;
                    }
                }
                snapshot.push_back(std::move(s));
            }
            const auto target = reducer::resolveRumble(snapshot, id);
            if (!target.valid()) { return; }
            // Rumble = vibration only; the light bar has its own return path via
            // MSG_LIGHTBAR. Actuation is marshalled onto the SDL thread inside
            // applyRumble (OutputCommandQueue) — the threading model is untouched.
            bridge_->applyRumble(target.deviceId, rm.strongMagnitude, rm.weakMagnitude,
                                 rm.durationMs);
        });
        // Parallel handler for the dedicated MSG_LIGHTBAR stream (Task 1.4).
        // Resolves the same slot/connection mapping and forwards to the
        // bridge's standalone applyLightbar — independent of rumble. Gated by
        // the light-bar setting: "Off" suppresses the colour entirely.
        conn->setLightbarHandler([this, id](const net::SatelliteClient::LightbarMessage& lm) {
            const auto color =
                lightbarColorFromLightbarMessage(lm, featureSettings_->lightbarFollowGame());
            if (!color) { return; }
            QString deviceId;
            const auto bindings = hub_->bindings();
            for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
                if (it.value() == id) {
                    deviceId = it.key();
                    break;
                }
            }
            if (deviceId.isEmpty()) { return; }
            bridge_->applyLightbar(deviceId, color->r, color->g, color->b);
        });
    }
}

void AppModel::start() {
    bridge_->start();
    wifi_->autoReconnectAll();
    autoReconnectTimer_->start();
}

void AppModel::clearPairingTarget() {
    if (!state_.pairingTarget.has_value()) { return; }
    state_.pairingTarget.reset();
    emit stateChanged();
}

void AppModel::onHubChanged() {
    state_.connections = hub_->connections();
    rebuild();
}

void AppModel::onBridgeDevicesChanged() {
    // Push each newly-attached device's persisted deadzone profile into the
    // processor exactly once (the hot-path rule: configure at device-add, never
    // per event). The SDL bridge already installed its default at attach; if the
    // user saved a per-device override we overwrite it here with the stored
    // value. Devices that drop out are pruned so a re-plug re-applies.
    const auto devices = bridge_->devices();
    QSet<QString> present;
    for (const auto& d : devices) {
        present.insert(d.id);
        if (deadzonePushedDevices_.contains(d.id)) { continue; }
        deadzonePushedDevices_.insert(d.id);
        if (auto dz = deadzoneRepo_.deadzonesFor(d.id)) {
            processor_.setDeadzones(d.id.toStdString(), {dz->stickFlat, dz->triggerFlat});
        }
    }
    for (auto it = deadzonePushedDevices_.begin(); it != deadzonePushedDevices_.end();) {
        it = present.contains(*it) ? std::next(it) : deadzonePushedDevices_.erase(it);
    }

    // A new device only matters for routing if a connection is already bound
    // to its slot id, so re-trigger the same rebuild path.
    rebuild();
}

void AppModel::applyDeadzones(const QString& deviceId, const input::deadzone::Deadzones& dz) {
    // Push to the live processor (once, off the hot path) so a slider change
    // takes effect without a re-attach. Persistence is the settings page's job
    // (it writes the DeadzoneRepository before emitting); we only apply here.
    processor_.setDeadzones(deviceId.toStdString(), {dz.stickFlat, dz.triggerFlat});
}

void AppModel::onWifiEvent(const net::ConnectionEvent& evt) {
    switch (evt.kind) {
    case net::ConnectionEventKind::PairingRequired:
        state_.pairingTarget = evt.server;
        emit stateChanged();
        break;
    case net::ConnectionEventKind::Error:
        emit errorMessage(evt.message);
        break;
    }
}

void AppModel::rebuild() {
    QList<models::ControllerSlot> next;
    // Windows is physical-controllers-only — no virtual touch overlay, so
    // we never seed a "Virtual Controller" slot. Matches dish-mac (PR #7);
    // dish-linux carries the slot as a placeholder for a future feature
    // that hasn't materialised on either desktop platform.
    for (const auto& d : bridge_->devices()) {
        models::ControllerSlot s;
        s.id = d.id;
        s.name = d.name;
        // Carry the SDL-detected motion capability through to the UI so the
        // slot card can show whether this pad has an IMU.
        s.capabilities.hasMotion = d.motionCapable;
        // Likewise the addressable-LED capability — drives the lightbar chip
        // and tells the hub when to advertise CAP_LIGHTBAR on bind.
        s.capabilities.hasLightbar = d.hasLightbar;
        // Carry the latest battery sample through so the slot card's battery
        // chip can show charge — controller's own for a wireless pad, the
        // host machine's for a wired/unknown one.
        s.capabilities.batteryLevel = d.batteryLevel;
        s.capabilities.batteryStatus = d.batteryStatus;
        next.append(s);
    }

    // Cross-reference bindings from the hub.
    const auto bindings = hub_->bindings();
    for (auto& s : next) {
        const auto cid = bindings.value(s.id);
        if (!cid.isEmpty()) {
            s.boundConnectionId = cid;
            s.boundStatus = hub_->summary(cid);
        }
    }
    state_.slotList = std::move(next);

    // Surface "a session is being established" so the dashboard can show an
    // indeterminate spinner. In protocol-1 topology rides REST (no per-add UDP
    // ACK poll), so "busy" is a session in its Linking handshake.
    bool busy = false;
    for (auto* conn : wifi_->connections()) {
        if (conn->state() == net::SessionState::Linking) {
            busy = true;
            break;
        }
    }
    state_.busy = busy;

    // Update the routing tables to mirror the new slot/binding shape.
    QHash<QString, net::ConnectionHub::ReportSender> nextRouting;
    QHash<QString, net::ConnectionHub::MotionSender> nextMotion;
    QHash<QString, net::ConnectionHub::BatterySender> nextBattery;
    QHash<QString, net::ConnectionHub::TouchpadSender> nextTouchpad;
    for (const auto& slot : state_.slotList) {
        if (auto sender = hub_->reportSenderForSlot(slot.id)) {
            nextRouting.insert(slot.id, std::move(sender));
        }
        if (auto sender = hub_->motionSenderForSlot(slot.id)) {
            nextMotion.insert(slot.id, std::move(sender));
        }
        if (auto sender = hub_->batterySenderForSlot(slot.id)) {
            nextBattery.insert(slot.id, std::move(sender));
        }
        if (auto sender = hub_->touchpadSenderForSlot(slot.id)) {
            nextTouchpad.insert(slot.id, std::move(sender));
        }
    }
    {
        std::lock_guard<std::mutex> lock(routingMtx_);
        routing_ = std::move(nextRouting);
        motionRouting_ = std::move(nextMotion);
        batteryRouting_ = std::move(nextBattery);
        touchpadRouting_ = std::move(nextTouchpad);
    }

    // Drive the display-sleep inhibitor off bindings × hub.connections. The
    // 0↔positive transitions inside ScreenWakeController set / clear the
    // SetThreadExecutionState flag; intermediate same-count emissions are
    // no-ops so a noisy hub feed doesn't thrash the kernel.
    QHash<QString, models::LinkState> connectionStates;
    for (const auto& summary : state_.connections) {
        connectionStates.insert(summary.id, summary.live);
    }
    const int streamingCount =
        util::ScreenWakeController::streamingCount(bindings, connectionStates);
    wake_.update(streamingCount);

    emit stateChanged();
}

// ── Workstream 2c: catalog-driven Emulate picker ─────────────────────────────

int AppModel::resolveControllerType(const QString& slotId) const {
    // 1) The user's Emulate override (keyed by the slot's bound connection).
    const QString connId = hub_->bindings().value(slotId);
    if (!connId.isEmpty()) {
        if (auto override = typeStore_.typeFor(connId.toStdString(), slotId.toStdString())) {
            return *override;
        }
    }
    // 2) The pad's SDL hardware classification.
    for (const auto& d : bridge_->devices()) {
        if (d.id == slotId) { return d.controllerType; }
    }
    // 3) Default Xbox.
    return proto::kControllerTypeXbox;
}

int AppModel::currentTypeFor(const QString& slotId) const { return resolveControllerType(slotId); }

QList<composer::PickableType> AppModel::pickableTypesFor(const QString& slotId) const {
    const QString connId = hub_->bindings().value(slotId);
    if (connId.isEmpty()) { return {}; }
    if (auto* conn = wifi_->get(connId)) {
        if (auto cached = catalogRepo_.cached(conn->server().id())) {
            return composer::offerableTypes(*cached);
        }
    }
    // Fall back to whatever the composer currently projects (the last fetched
    // catalog), so a freshly-bound slot still offers the known types.
    return catalogComposer_.state().value();
}

void AppModel::setSlotControllerType(const QString& slotId, int type) {
    const QString connId = hub_->bindings().value(slotId);
    if (connId.isEmpty()) { return; } // unbound: nothing to emulate
    typeStore_.setType(connId.toStdString(), slotId.toStdString(), type);
    // Re-attach the slot so the new descriptor (carrying the chosen type) is
    // PUT to the satellite — the chosen type reaches the wire. bind() re-reads
    // resolveControllerType, which now returns the override.
    hub_->bind(slotId, connId);
}

void AppModel::refreshCatalogForSlot(const QString& slotId) {
    const QString connId = hub_->bindings().value(slotId);
    if (connId.isEmpty()) { return; }
    auto* conn = wifi_->get(connId);
    if (conn == nullptr) { return; }
    const auto server = conn->server();
    const QString satId = server.id();
    // BCP-47 locale chain for Accept-Language; the satellite falls back to en.
    const QString acceptLanguage = QLocale().bcp47Name();
    catalogRepo_.catalogFor(server, satId, acceptLanguage,
                            [this](const std::optional<models::CatalogDto>& catalog) {
                                if (!catalog.has_value()) { return; }
                                // Feed the composer; its distinct-until-changed
                                // suppresses a no-op (304-served identical) update.
                                catalogSnapshot_.set(composer::CatalogSnapshot{*catalog});
                            });
}

} // namespace dish
