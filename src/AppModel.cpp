// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "AppModel.h"

#include "LightbarRouting.h"
#include "composer/StreamingSlotCount.h"
#include "core/input/UsbReportParsers.h"
#include "core/reducer/PickerVisibility.h"
#include "core/reducer/RumbleRouting.h"
#include "core/reducer/SlotPathFields.h"
#include "core/reducer/TouchpadRouting.h"
#include "core/reducer/UsbTwinDedup.h"

#include <chrono>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include <QApplication>
#include <QLocale>

namespace dish {

namespace {

// Stamp the USB path fields onto a slot from the pure reducer mapper. Lives here
// (not in the loop body) so both the SDL-slot and synthetic-slot rebuild arms
// thread through the SAME one-line cross-reference. Keeping the derivation in the
// pure reducer::slotPathFields makes it unit-testable without a live manager.
void stampSlotPath(models::ControllerSlot& s, int vendorId, int productId,
                   const std::map<int, reducer::UsbController>& controllers) {
    const reducer::SlotPathFields f = reducer::slotPathFields(vendorId, productId, controllers);
    s.pathSupported = f.supported;
    s.pathPhase = f.phase;
    s.desiredPath = f.desired;
    s.directFailure = f.failure;
}

} // namespace

AppModel::AppModel(QObject* parent)
    : AppModel(std::make_unique<util::SetThreadExecutionStateInhibitor>(), parent) {}

AppModel::AppModel(std::unique_ptr<util::DisplaySleepInhibitor> inhibitor, QObject* parent)
    : QObject(parent), store_(std::make_unique<net::ConnectionStore>()),
      wifi_(new net::WifiConnectionManager(store_.get(), this)),
      hub_(new net::ConnectionHub(wifi_, store_.get(), this)),
      connections_(new composer::ConnectionCoordinator(wifi_, hub_, this)),
      bridge_(new input::SDLGamepadBridge(&processor_, this)),
      featureSettings_(new FeatureSettings(this)), autoReconnectTimer_(new QTimer(this)),
      inhibitor_(std::move(inhibitor)), wakeComposer_(streamingSlotCount_, shouldKeepScreenOn_),
      wakeController_(wakeComposer_.state(), inhibitor_.get()),
      themeController_(themeStore_.state(), qApp),
      crashController_(crashStore_.state(), &crashBackend_),
      catalogHttp_(new net::HTTPClient(this)), catalogRepo_(catalogHttp_),
      motionEnabledStore_(&motionPrefRepo_), joystickRemapStore_(&joystickRemapRepo_),
      catalogSnapshot_(composer::CatalogSnapshot{}), catalogComposer_(catalogSnapshot_),
      usbPathStore_(&usbPathRepo_), usbObserver_(this), usbScanTimer_(new QTimer(this)),
      inputRateTimer_(new QTimer(this)) {
    QObject::connect(hub_, &net::ConnectionHub::changed, this, &AppModel::onHubChanged);
    QObject::connect(bridge_, &input::SDLGamepadBridge::devicesChanged, this,
                     &AppModel::onBridgeDevicesChanged);
    // Forward the bridge's raw-input capture up to the view-model (which maps the
    // deviceId → slotId and re-emits only for the capturing slot). A direct signal
    // relay — the bridge already QueuedConnection-hops to this (GUI) thread.
    QObject::connect(bridge_, &input::SDLGamepadBridge::rawJoystickInput, this,
                     &AppModel::rawJoystickInput);
    QObject::connect(wifi_, &net::WifiConnectionManager::connectionEvent, this,
                     &AppModel::onWifiEvent);
    // A controller-registration rejection (the satellite refused a slot's
    // descriptor) is rolled back by ConnectionHub (the binding reverts), but the
    // user saw their bind silently undo with no reason. Surface it: the only
    // error path that previously produced ZERO user feedback. Routed to the same
    // one-shot toast channel as every other transient error.
    QObject::connect(wifi_, &net::WifiConnectionManager::slotRegistrationFailed, this,
                     [this](const QString&) {
                         emit errorMessage(
                             tr("The satellite wouldn't accept that controller — binding undone."));
                     });
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

    // And the controller type (Xbox / PlayStation) so the slot's REST
    // descriptor declares the right `type` — a DualSense → virtual DS4. The
    // user's Emulate override (ControllerTypeStore, Workstream 2c) wins over the
    // SDL hardware classification; resolveControllerType applies that ladder so
    // the choice is threaded into the descriptor PUT.
    hub_->setControllerTypeFn(
        [this](const QString& slotId) { return resolveControllerType(slotId); });

    // ── USB-direct (raw-HID) claim path ──────────────────────────────────────
    // Build the real Windows raw-HID gateway + the claim driver, feeding decoded
    // reports into the SAME processor_ as the SDL path. The driver is dormant
    // until start() arms the scan timer; reconcile() then enumerates HID pads and
    // auto-claims the verified fast-lane models (DualSense / DS4 / 8BitDo) while
    // every other / failed pad stays on SDL via the FSM's Routed phase.
    usbGateway_ = std::make_unique<source::usb::WinHidGateway>();
    usbManager_ = std::make_unique<source::usb::UsbGamepadManager>(usbGateway_.get(), &processor_,
                                                                   &usbPathStore_, &usbObserver_);
    // The scan timer drives reconcile() (idempotent re-enumeration) + the
    // poll-rate sampler. 1 s mirrors android's foreground reconcile cadence; it is
    // off the hot path (enumeration only, never per-report). Fires on the main
    // thread — the only thread that mutates the FSM.
    usbScanTimer_->setInterval(1000);
    QObject::connect(usbScanTimer_, &QTimer::timeout, this, &AppModel::pollUsbDirect);

    // ── Raw-joystick remap → bridge ──────────────────────────────────────────
    // Push every saved remap into the bridge now, then re-push on any store
    // republish (a setRemap/clearRemap from the page). The hot path then decodes a
    // generic pad under its corrected layout. emitCurrent=false because we push
    // explicitly below — the subscription handles only subsequent changes.
    joystickRemapSub_ = joystickRemapStore_.state().subscribe(
        [this](const source::JoystickRemapMap&) { pushJoystickRemapsToBridge(); },
        /*emitCurrent=*/false);
    pushJoystickRemapsToBridge();

    // Arm the wake controller: it subscribes the WakeStateComposer and applies
    // the current WakeState immediately (idempotent start). From here, setting
    // streamingSlotCount_ in recompute() flows count -> WakeState -> inhibitor.
    wakeController_.start();

    // Arm the theme controller: it subscribes the ThemePreferenceStore and
    // applies the persisted (or System-resolved) palette immediately, re-theming
    // the live QApplication. start() applying the current value == android's
    // applyPersistedMode(). The Source derives the mode; this Controller effects
    // the palette — they cannot drift (§4.3 rule 2).
    themeController_.start();

    // Arm the crash-reporting controller: it subscribes the CrashReportingStore
    // and forwards the current opt-in to the (no-op) backend immediately. Its
    // stop() is a deliberate no-op so the opt-in survives teardown (D4).
    crashController_.start();

    // ── Live input-rate measurement (android parity) ─────────────────────────
    // The store samples the processor's per-device counters through the pure
    // InputRateTracker. Its CounterSource borrows processor_ (alive for the whole
    // AppModel lifetime). slotId is the SDL device id / USB-direct synthetic key
    // string — exactly the key processor_.publish() counts under, so the lookup
    // lines up with no translation.
    inputRateStore_ = std::make_unique<source::InputRateStore>(
        [this](const std::string& slotId) -> source::SlotInputCounters {
            const auto c = processor_.inputCounters(slotId);
            return source::SlotInputCounters{c.gamepadEvents, c.motionEvents};
        });
    inputRatesSub_ = inputRateStore_->state().subscribe(
        [this](const source::SlotInputRatesMap& rates) { onInputRatesChanged(rates); },
        /*emitCurrent=*/false);
    // Drive the store at ~1 Hz on the main thread — the same cadence as the
    // telemetry footer and android's sub-second sampling loop. We pump sampleAt()
    // with the steady clock rather than the store's own QTimer so the sampling
    // lives on the AppModel's thread alongside the slot-list state it patches.
    inputRateTimer_->setInterval(1000);
    QObject::connect(inputRateTimer_, &QTimer::timeout, this, [this] {
        const auto nowUs =
            static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(
                                           std::chrono::steady_clock::now().time_since_epoch())
                                           .count());
        inputRateStore_->sampleAt(nowUs);
    });

    rebuild();
}

AppModel::~AppModel() {
    // Stop the input thread first so no further SDL reports race teardown, then
    // tear down the USB-direct path. usbManager_ destructs before usbGateway_
    // (reverse declaration order) — but the manager holds only a borrowed gateway
    // pointer, so the gateway's own destructor is what releases the live claims
    // (stops every read loop + closes the HID handles). Reset the timer-bound
    // objects explicitly so no queued pollUsbDirect fires against a half-torn-down
    // manager.
    bridge_->stop();
    usbScanTimer_->stop();
    inputRateTimer_->stop();
    // Drop the subscription before the store so no folded emission races teardown.
    inputRatesSub_ = arch::Observable<source::SlotInputRatesMap>::Subscription{};
    inputRateStore_.reset();
    // Same for the remap subscription — drop it before teardown so a late store
    // republish can't push into a half-gone bridge.
    joystickRemapSub_ = arch::Observable<source::JoystickRemapMap>::Subscription{};
    usbManager_.reset();
    usbGateway_.reset();
}

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
    // Bring up USB-direct: an immediate scan so a pad plugged in before launch is
    // claimed promptly, then the periodic reconcile + poll-rate sampling.
    pollUsbDirect();
    usbScanTimer_->start();
    // Begin live input-rate sampling. The first tick only baselines each tracker
    // (reports 0), so numbers appear from the second tick on — same as android.
    inputRateTimer_->start();
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

    // A freshly-attached generic pad with a saved remap must decode under it from
    // the first report, so re-push the saved set on every device change. Cheap +
    // idempotent (the bridge just overwrites its small per-model map).
    pushJoystickRemapsToBridge();

    // An SDL device appearing / disappearing is a framework up/down signal for the
    // USB FSM (the "framework" path on Windows is SDL) — feed the deltas so a
    // claim-failure / Standard pick can settle on the live SDL device.
    syncFrameworkPresence();

    // A new device only matters for routing if a connection is already bound
    // to its slot id, so re-trigger the same rebuild path (which also recomputes
    // the twin-dedup suppression off the fresh device list).
    rebuild();
}

void AppModel::applyDeadzones(const QString& deviceId, const input::deadzone::Deadzones& dz) {
    // Push to the live processor (once, off the hot path) so a slider change
    // takes effect without a re-attach. Persistence is the settings page's job
    // (it writes the DeadzoneRepository before emitting); we only apply here.
    processor_.setDeadzones(deviceId.toStdString(), {dz.stickFlat, dz.triggerFlat});
}

void AppModel::pushJoystickRemapsToBridge() {
    // Walk the store's whole keyed map and install each into the bridge. The key
    // is the "%04x:%04x" vid:pid string; split it back into the two ints the
    // bridge keys by. A malformed key (never produced by joystickRemapKeyFor) is
    // skipped rather than crashing — forward-compat with a future key format.
    for (const auto& [key, remap] : joystickRemapStore_.state().value()) {
        const auto colon = key.find(':');
        if (colon == std::string::npos) { continue; }
        bool okV = false;
        bool okP = false;
        const int vendorId =
            static_cast<int>(QString::fromStdString(key.substr(0, colon)).toUInt(&okV, 16));
        const int productId =
            static_cast<int>(QString::fromStdString(key.substr(colon + 1)).toUInt(&okP, 16));
        if (!okV || !okP) { continue; }
        bridge_->setJoystickRemap(vendorId, productId, remap);
    }
}

input::JoystickRemap AppModel::remapFor(int vendorId, int productId) const {
    // The stored override if any, else today's default layout — exactly what the
    // page renders (and the bridge applies).
    if (const auto r = joystickRemapStore_.remapFor(vendorId, productId)) { return *r; }
    return input::JoystickRemap{};
}

void AppModel::setJoystickRemap(int vendorId, int productId, const input::JoystickRemap& remap) {
    // Persist + republish; the store subscription re-pushes the whole set into the
    // bridge, so the new layout takes effect on the next report.
    joystickRemapStore_.setRemap(vendorId, productId, remap);
}

void AppModel::clearJoystickRemap(int vendorId, int productId) {
    // Drop the override (the store republish re-pushes the remaining set), then
    // explicitly clear it in the bridge — the store-driven re-push only INSTALLS
    // present entries, it never erases a dropped one, so the bridge would keep the
    // stale remap without this.
    joystickRemapStore_.clearRemap(vendorId, productId);
    bridge_->clearJoystickRemap(vendorId, productId);
}

void AppModel::setInputCaptureEnabled(bool enabled) { bridge_->setJoystickCaptureEnabled(enabled); }

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

void AppModel::onUsbNotice(const reducer::UsbController& c, reducer::UsbNotice notice) {
    // The pad name for the banner; fall back to a generic noun when the FSM
    // controller has no name yet.
    const QString name = c.name.empty() ? tr("Controller") : QString::fromStdString(c.name);
    QString msg;
    switch (notice) {
    case reducer::UsbNotice::SwitchToDirectFailed:
        msg = tr("Couldn't switch %1 to Direct mode — keeping it on Standard.").arg(name);
        break;
    case reducer::UsbNotice::NeedsReplug:
        msg = tr("%1 needs to be unplugged and reconnected.").arg(name);
        break;
    case reducer::UsbNotice::RolledBackToDirect:
        msg = tr("%1 stayed on Direct mode.").arg(name);
        break;
    case reducer::UsbNotice::RestoreFailed:
        msg = tr("Couldn't return %1 to Standard mode.").arg(name);
        break;
    }
    if (!msg.isEmpty()) { emit errorMessage(msg); }
}

void AppModel::pollUsbDirect() {
    if (usbManager_ == nullptr) { return; }
    // Idempotent re-enumeration: tracks freshly-plugged HID pads + drives each
    // toward its resolved path (auto-Direct for verified fast-lane models). A pad
    // that fails to claim falls back to SDL via the FSM, so this never regresses
    // a working SDL pad.
    usbManager_->reconcile();

    // Sample the per-device poll rate off the gateway's completion counters. The
    // sampler is pure (clock-injected); we feed it the present synthetic ids and a
    // count lookup. The measured rate is currently informational (the live-stats
    // surface reads it on android); we drain it so the snapshot map stays bounded
    // and a re-attached id starts fresh.
    std::vector<int> present;
    for (const auto& [key, c] : usbManager_->controllers()) {
        if (c.phase == reducer::UsbPhase::Direct && c.syntheticId.has_value()) {
            present.push_back(*c.syntheticId);
        }
    }
    const auto nowMs =
        static_cast<std::int64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count());
    source::usb::WinHidGateway* gw = usbGateway_.get();
    const auto updates = usbPollSampler_.sampleAll(nowMs, present, [gw](int id) -> std::int64_t {
        return gw != nullptr ? gw->completionCount(id) : 0;
    });
    // Surface the measured poll rate per synthetic so the slot card can show it
    // for a USB-direct pad (android renders this as the Direct path's measured
    // Hz). The sampler is keyed by syntheticId; the slot id the UI uses is the
    // controllers() map key string, so translate syntheticId -> key here while we
    // still hold both. A synthetic's first sample emits no update (baseline only)
    // and an idle one emits 0 — both are fine to publish (0 reads as "pending").
    if (!updates.empty()) {
        QHash<int, int> bySyntheticId;
        for (const auto& u : updates) { bySyntheticId.insert(u.deviceId, u.rateHz); }
        bool changed = false;
        for (const auto& [key, c] : usbManager_->controllers()) {
            if (c.phase != reducer::UsbPhase::Direct || !c.syntheticId.has_value()) { continue; }
            const auto it = bySyntheticId.constFind(*c.syntheticId);
            if (it == bySyntheticId.constEnd()) { continue; }
            if (usbPollRateHz_.value(key, -1) != it.value()) {
                usbPollRateHz_.insert(key, it.value());
                changed = true;
            }
        }
        // Prune synthetics that are gone so a stale rate can't linger on a reused
        // key, then repaint if anything moved (the slot card reads it via state).
        for (auto it = usbPollRateHz_.begin(); it != usbPollRateHz_.end();) {
            const auto cit = usbManager_->controllers().find(it.key());
            const bool live = cit != usbManager_->controllers().end() &&
                              cit->second.phase == reducer::UsbPhase::Direct;
            if (live) {
                ++it;
            } else {
                it = usbPollRateHz_.erase(it);
                changed = true;
            }
        }
        if (changed) { rebuild(); }
    }
}

void AppModel::onUsbDirectChanged() {
    // A synthetic appeared / vanished (or its phase changed). Recompute the slot
    // list + the twin-dedup suppression off the fresh controller set. rebuild()
    // does both; it is cheap and already the single place the routing table is
    // rebuilt. Safe to call re-entrantly from a UsbDirectObserver effect — the
    // manager runs effects with its own lock released, and rebuild() takes only a
    // fresh snapshot of controllers().
    rebuild();

    // A Direct->Standard/Auto pick parks the controller in AwaitingFramework while
    // its synthetic is dropped (this callback fires on that change). On Windows the
    // SDL twin never left bridge_->devices(), so syncFrameworkPresence's appearance
    // diff won't re-fire FrameworkUp — settle it here instead. DEFERRED to a queued
    // call so it runs after the current effect loop (this very callback came from
    // inside applyEvent's effect dispatch) has fully unwound: settling re-enters
    // applyEvent, and doing that re-entrantly would mutate FSM state mid-dispatch.
    // The settle is idempotent (a Routed controller is no longer AwaitingFramework),
    // so the queued pass terminates after one settling step per controller.
    QMetaObject::invokeMethod(
        this, [this] { settleAwaitingFrameworkControllers(); }, Qt::QueuedConnection);
}

void AppModel::settleAwaitingFrameworkControllers() {
    if (usbManager_ == nullptr) { return; }
    // Build the set of framework VID:PIDs present RIGHT NOW from the live device
    // list — not the last-pass cache — so a device that has genuinely gone is
    // absent here and is left to time out into RestoreStuck/NeedsReplug rather than
    // getting a spurious FrameworkUp.
    QSet<int> present;
    for (const auto& d : bridge_->devices()) {
        if (d.vendorId == 0 || d.productId == 0) { continue; }
        present.insert((d.vendorId << 16) | (d.productId & 0xFFFF));
    }
    for (const auto& [key, c] : usbManager_->controllers()) {
        if (reducer::shouldSettleAwaitingFramework(c.phase, present.contains(key))) {
            usbManager_->onFrameworkUp(c.vendorId, c.productId, key);
        }
    }
}

void AppModel::syncFrameworkPresence() {
    if (usbManager_ == nullptr) { return; }
    // Diff the SDL device VID:PIDs against the last pass and emit FrameworkUp for
    // newly-present models, FrameworkDown for vanished ones. The FSM uses these to
    // settle AwaitingFramework -> Routed (a Standard pick / claim-failure rolling
    // back to the live SDL device). A frameworkId is needed by the FSM's bind
    // effect; we use the model's vpKey as a stable surrogate id (the Windows path
    // binds by slot-id string, not the numeric framework id, so its exact value is
    // immaterial — only up/down transitions matter).
    QSet<int> current;
    for (const auto& d : bridge_->devices()) {
        if (d.vendorId == 0 || d.productId == 0) { continue; }
        const int key = (d.vendorId << 16) | (d.productId & 0xFFFF);
        current.insert(key);
        if (!lastFrameworkVpKeys_.contains(key)) {
            usbManager_->onFrameworkUp(d.vendorId, d.productId, key);
        }
    }
    for (int key : lastFrameworkVpKeys_) {
        if (!current.contains(key)) {
            const int vendorId = (key >> 16) & 0xFFFF;
            const int productId = key & 0xFFFF;
            usbManager_->onFrameworkDown(vendorId, productId);
        }
    }
    lastFrameworkVpKeys_ = current;

    // Level-triggered settle, not only the appearance edge above: on Windows the
    // SDL twin is continuously present while a pad is claimed for Direct, so a
    // controller parked in AwaitingFramework by a Direct->Standard release would
    // never see a fresh FrameworkUp edge. Drive one for any controller still
    // awaiting whose framework device is present in the *current* set (a genuinely
    // gone device is not in `current`, so its Timeout->RestoreStuck path is intact).
    for (const auto& [key, c] : usbManager_->controllers()) {
        if (reducer::shouldSettleAwaitingFramework(c.phase, current.contains(key))) {
            usbManager_->onFrameworkUp(c.vendorId, c.productId, key);
        }
    }
}

void AppModel::rebuild() {
    QList<models::ControllerSlot> next;
    // Windows is physical-controllers-only — no virtual touch overlay, so
    // we never seed a "Virtual Controller" slot. Matches dish-mac (PR #7);
    // dish-linux carries the slot as a placeholder for a future feature
    // that hasn't materialised on either desktop platform.

    // Twin-dedup: a pad visible to BOTH SDL/XInput and the raw-HID gateway must
    // stream via exactly one path. Build the active USB-direct synthetics (FSM
    // phase Direct) and the SDL routed devices, then ask the pure reducer which
    // SDL ids are hidden by a synthetic twin. Push the hidden set into the bridge
    // so its INPUT/MOTION/TOUCHPAD for those ids never reach the wire; the hidden
    // SDL slots are also dropped from the slot list (so they get no binding /
    // routing), and the synthetics are added in their place. Mirrors android's
    // MainViewModel slot derivation (routedTwinIdsHiddenBySynthetics).
    std::vector<reducer::SyntheticTwin> synthetics;
    std::map<int, reducer::UsbController> controllers;
    if (usbManager_ != nullptr) { controllers = usbManager_->controllers(); }
    for (const auto& [key, c] : controllers) {
        if (c.phase == reducer::UsbPhase::Direct) {
            synthetics.push_back({c.vendorId, c.productId});
        }
    }
    const auto sdlDevices = bridge_->devices();
    std::vector<reducer::RoutedDevice> routed;
    routed.reserve(static_cast<std::size_t>(sdlDevices.size()));
    for (const auto& d : sdlDevices) {
        routed.push_back({d.id.toStdString(), d.vendorId, d.productId, /*disconnecting=*/false});
    }
    const std::set<std::string> hidden = reducer::suppressedRoutedIds(synthetics, routed);
    {
        std::unordered_set<std::string> hiddenSet(hidden.begin(), hidden.end());
        bridge_->setSuppressedDeviceIds(hiddenSet);
    }

    for (const auto& d : sdlDevices) {
        // Skip the SDL twin of an active USB-direct claim — it streams via raw-HID.
        if (hidden.count(d.id.toStdString()) != 0) { continue; }
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
        // Stamp the USB path state by cross-referencing the matching
        // UsbController by this SDL device's (vid, pid). A pad the raw-HID
        // gateway never enumerates (Xbox/XInput) has no controller -> the pure
        // mapper leaves pathSupported=false and the card hides the control.
        // Only a raw joystick (not an SDL game controller) decodes through the
        // remappable mapJoystick path; carry the flag so the page entry shows
        // for exactly those. Synthetics below stay false (default).
        s.remappable = d.isRawJoystick;
        stampSlotPath(s, d.vendorId, d.productId, controllers);
        next.append(s);
    }

    // Add a slot for each USB-direct-claimed pad (FSM phase Direct). Its id is the
    // model key string the read-loop publishes under (UsbGamepadManager::doClaim),
    // so binding it routes the decoded reports through the existing hub machinery.
    // hasMotion is the model's IMU capability (the decoder emits gyro/accel for
    // DS4 / DualSense / Switch Pro).
    for (const auto& [key, c] : controllers) {
        if (c.phase != reducer::UsbPhase::Direct) { continue; }
        models::ControllerSlot s;
        s.id = QString::fromStdString(std::to_string(key));
        s.name = QString::fromStdString(c.name);
        s.capabilities.hasMotion = input::usbparse::parserHasImu(
            input::usbparse::parserForDevice(c.vendorId, c.productId));
        s.capabilities.hasLightbar = false;
        // Mark it as a USB-direct synthetic so the slot card shows its gamepad Hz
        // as a live (continuously-streaming) measurement, and attach the latest
        // independently-measured poll rate (URB completion rate) for it.
        s.usbDirect = true;
        s.liveRates.directPollHz = usbPollRateHz_.value(key, 0);
        // A synthetic IS a USB-direct controller, so it is always
        // path-supported; the mapper reads the phase/desired/failure off its own
        // controller entry (keyed by vid/pid, which round-trips to this key).
        stampSlotPath(s, c.vendorId, c.productId, controllers);
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
        // Stamp the latest measured stream rates (gamepad/motion Hz + peaks) the
        // InputRateStore folded into liveRatesBySlot_, preserving them across this
        // rebuild. directPollHz was already set on the synthetic above from
        // usbPollRateHz_; keep it.
        const auto rit = liveRatesBySlot_.constFind(s.id);
        if (rit != liveRatesBySlot_.constEnd()) {
            const int keepPoll = s.liveRates.directPollHz;
            s.liveRates = rit.value();
            s.liveRates.directPollHz = keepPoll;
        }
    }
    state_.slotList = std::move(next);

    // Keep the InputRateStore's tracked slot set in lockstep with the slot list,
    // so a freshly-attached pad gets a tracker and a departed one is dropped
    // (its tracker rebaselines on re-attach).
    syncInputRateDevices();

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
            nextRouting.insert(slot.id, sender);
        }
        if (auto sender = hub_->motionSenderForSlot(slot.id)) {
            nextMotion.insert(slot.id, sender);
        }
        if (auto sender = hub_->batterySenderForSlot(slot.id)) {
            nextBattery.insert(slot.id, sender);
        }
        if (auto sender = hub_->touchpadSenderForSlot(slot.id)) {
            nextTouchpad.insert(slot.id, sender);
        }
    }
    {
        std::lock_guard<std::mutex> lock(routingMtx_);
        routing_ = std::move(nextRouting);
        motionRouting_ = std::move(nextMotion);
        batteryRouting_ = std::move(nextBattery);
        touchpadRouting_ = std::move(nextTouchpad);
    }

    // Drive the display-sleep inhibitor off bindings × hub.connections. Setting
    // the streaming-slot-count Observable flows through WakeStateComposer (derive
    // WakeState) into WakeStateController (effect SetThreadExecutionState). The
    // composer's distinct-until-changed + the inhibitor's idempotent acquire/
    // release preserve the 0↔positive no-thrash contract — a noisy hub feed that
    // doesn't change the count never touches the OS power portal.
    QHash<QString, models::LinkState> connectionStates;
    for (const auto& summary : state_.connections) {
        connectionStates.insert(summary.id, summary.live);
    }
    streamingSlotCount_.set(composer::streamingSlotCount(bindings, connectionStates));

    emit stateChanged();
}

void AppModel::syncInputRateDevices() {
    if (!inputRateStore_) { return; }
    // Desired tracked set = the current slot ids.
    QSet<QString> present;
    for (const auto& s : state_.slotList) {
        present.insert(s.id);
        inputRateStore_->addDevice(s.id.toStdString()); // idempotent
    }
    // Drop trackers + cached rates for slots that vanished, so a later re-attach
    // re-baselines from 0 instead of inheriting a stale anchor / number.
    for (auto it = liveRatesBySlot_.begin(); it != liveRatesBySlot_.end();) {
        if (present.contains(it.key())) {
            ++it;
        } else {
            inputRateStore_->removeDevice(it.key().toStdString());
            it = liveRatesBySlot_.erase(it);
        }
    }
    // removeDevice for ids that were tracked but never had a cached-rate entry is
    // handled by the store's own idempotent no-op; the cache prune above covers
    // the common case (a slot that produced at least one rate emission).
}

void AppModel::onInputRatesChanged(const source::SlotInputRatesMap& rates) {
    // Project the store's emission into the model's value type and remember it so
    // a later slot-list rebuild keeps the numbers. directPollHz is owned by the
    // USB poll path (usbPollRateHz_), not the store, so it is preserved here.
    bool changed = false;
    for (const auto& [slotId, r] : rates) {
        const QString id = QString::fromStdString(slotId);
        models::SlotLiveRates next = liveRatesBySlot_.value(id);
        next.gamepadHz = r.gamepadHz;
        next.gamepadPeakHz = r.gamepadPeakHz;
        next.motionHz = r.motionHz;
        next.motionPeakHz = r.motionPeakHz;
        if (liveRatesBySlot_.value(id) != next) {
            liveRatesBySlot_.insert(id, next);
            changed = true;
        }
    }
    if (!changed) { return; }
    // Patch the live slot list in place + repaint. We avoid a full rebuild() (no
    // routing/twin-dedup recompute needed for a pure rate change) but reuse the
    // same stamping rule so directPollHz is retained.
    bool visibleChange = false;
    for (auto& s : state_.slotList) {
        const auto rit = liveRatesBySlot_.constFind(s.id);
        if (rit == liveRatesBySlot_.constEnd()) { continue; }
        models::SlotLiveRates merged = rit.value();
        merged.directPollHz = s.liveRates.directPollHz;
        if (s.liveRates != merged) {
            s.liveRates = merged;
            visibleChange = true;
        }
    }
    if (visibleChange) { emit stateChanged(); }
}

// ── Workstream 2c: catalog-driven Emulate picker ─────────────────────────────

int AppModel::resolveControllerType(const QString& slotId) const {
    const QString connId = hub_->bindings().value(slotId);
    if (connId.isEmpty()) { return proto::kControllerTypeXbox; }
    // The user's Emulate override wins; absent that the default is the bound
    // satellite catalog's first offered type (the picker's first row), then Xbox.
    const auto userOverride = typeStore_.typeFor(connId.toStdString(), slotId.toStdString());
    std::optional<models::CatalogDto> cached;
    if (auto* conn = wifi_->get(connId)) { cached = catalogRepo_.cached(conn->server().id()); }
    return reducer::seedControllerType(userOverride, cached);
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
    // Enter Loading (keeping any prior catalog as stale) so the picker can show a
    // spinner over the last-known types while the GET is in flight.
    catalogState_ = core::toLoading(catalogState_);
    emit catalogStateChanged();
    catalogRepo_.catalogFor(server, satId, acceptLanguage,
                            [this](const source::CatalogState& state) {
                                // Capture the full lifecycle — Loading already
                                // fired; this is the terminal Success/Error. The
                                // failure is no longer dropped: the picker binds
                                // catalogState() to show the cause + a retry.
                                catalogState_ = state;
                                // Feed the composer whenever we have data (fresh
                                // 200, 304-revalidated, or stale-served-on-error);
                                // its distinct-until-changed suppresses no-ops.
                                if (state.hasData()) {
                                    catalogSnapshot_.set(composer::CatalogSnapshot{*state.data});
                                }
                                emit catalogStateChanged();
                            });
}

} // namespace dish
