// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightManager.h"

#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"
#include "source/connection/NvstreamDiscovery.h"

#include <QSet>
#include <QSettings>

namespace dish::net {

QString moonlightPhaseToken(moonlight::SessionPhase phase) {
    switch (phase) {
    case moonlight::SessionPhase::Idle:
        return QStringLiteral("idle");
    case moonlight::SessionPhase::Pairing:
        return QStringLiteral("pairing");
    case moonlight::SessionPhase::Paired:
        return QStringLiteral("paired");
    case moonlight::SessionPhase::Launching:
        return QStringLiteral("launching");
    case moonlight::SessionPhase::RtspHandshake:
    case moonlight::SessionPhase::ControlConnecting:
        return QStringLiteral("connecting");
    case moonlight::SessionPhase::Streaming:
        return QStringLiteral("streaming");
    case moonlight::SessionPhase::Faltering:
        return QStringLiteral("faltering");
    case moonlight::SessionPhase::Closed:
        return QStringLiteral("closed");
    case moonlight::SessionPhase::Failed:
        return QStringLiteral("failed");
    }
    return QStringLiteral("idle");
}

QList<MoonlightHostRow> mergeMoonlightRows(const QList<models::MoonlightHost>& remembered,
                                           const QList<models::MoonlightHost>& discovered,
                                           const QHash<QString, QString>& phaseTokensById) {
    QList<MoonlightHostRow> rows;
    QSet<QString> seen;

    auto rowFor = [&](const models::MoonlightHost& h, bool isDiscovered) {
        MoonlightHostRow r;
        r.id = h.id();
        r.name = h.name.isEmpty() ? h.ip : h.name;
        r.ip = h.ip;
        r.paired = h.paired;
        r.discovered = isDiscovered;
        r.phaseToken = phaseTokensById.value(r.id, QStringLiteral("idle"));
        r.appName = h.lastAppName;
        r.deviceType = h.deviceType;
        return r;
    };

    for (const auto& h : remembered) {
        rows.append(rowFor(h, false));
        seen.insert(h.id());
    }
    for (const auto& h : discovered) {
        // A discovered host already remembered is folded, not duplicated.
        if (seen.contains(h.id())) {
            for (auto& r : rows) {
                if (r.id == h.id()) { r.discovered = true; }
            }
            continue;
        }
        rows.append(rowFor(h, true));
        seen.insert(h.id());
    }
    return rows;
}

MoonlightManager::MoonlightManager(std::shared_ptr<QSettings> settings, QObject* parent)
    : QObject(parent),
      repo_(std::make_unique<repository::MoonlightHostRepository>(std::move(settings))) {}

MoonlightManager::~MoonlightManager() {
    if (discoveryThread_.joinable()) { discoveryThread_.join(); }
    // Sessions are QObject children of this manager; Qt deletes them.
}

QList<MoonlightHostRow> MoonlightManager::hostRows() const {
    QHash<QString, QString> phaseTokens;
    for (auto it = sessions_.cbegin(); it != sessions_.cend(); ++it) {
        if (it.value() != nullptr) {
            phaseTokens.insert(it.key(), moonlightPhaseToken(it.value()->phase()));
        }
    }
    return mergeMoonlightRows(repo_->hosts(), discovered_, phaseTokens);
}

void MoonlightManager::startDiscovery() {
    if (scanning_) { return; }
    if (discoveryThread_.joinable()) { discoveryThread_.join(); }
    scanning_ = true;
    emit scanningChanged();
    discoveryThread_ = std::thread([this] {
        const auto found = NvstreamDiscovery::discover();
        QMetaObject::invokeMethod(
            this,
            [this, found] {
                discovered_ = found;
                scanning_ = false;
                emit scanningChanged();
                emit hostsChanged();
            },
            Qt::QueuedConnection);
    });
}

void MoonlightManager::addManualHost(const QString& ip, const QString& name) {
    if (ip.isEmpty()) { return; }
    models::MoonlightHost h;
    h.ip = ip;
    h.name = name.isEmpty() ? ip : name;
    repo_->rememberHost(h);
    emit hostsChanged();
}

std::optional<models::MoonlightHost> MoonlightManager::hostById(const QString& id) const {
    for (const auto& h : repo_->hosts()) {
        if (h.id() == id) { return h; }
    }
    for (const auto& h : discovered_) {
        if (h.id() == id) { return h; }
    }
    return std::nullopt;
}

MoonlightSession* MoonlightManager::ensureSession(const models::MoonlightHost& host) {
    const QString id = host.id();
    if (auto* existing = sessions_.value(id, nullptr)) { return existing; }
    if (!identity_.has_value()) {
        identity_ = repo_->getOrCreateIdentity();
        if (!identity_.has_value()) { return nullptr; } // OpenSSL failure
    }
    auto* session = new MoonlightSession(host, *identity_, repo_.get(), this);
    sessions_.insert(id, session);
    QObject::connect(session, &MoonlightSession::phaseChanged, this, [this, id] {
        emit sessionPhaseChanged(id);
        emit hostsChanged();
    });
    QObject::connect(session, &MoonlightSession::pairingFinished, this, [this, id](bool ok) {
        emit pairingFinished(id, ok);
        emit hostsChanged();
    });
    QObject::connect(session, &MoonlightSession::rumbleReceived, this,
                     [this, id](int n, int lo, int hi) { emit rumbleReceived(id, n, lo, hi); });
    QObject::connect(
        session, &MoonlightSession::rgbLedReceived, this,
        [this, id](int n, int r, int g, int b) { emit rgbLedReceived(id, n, r, g, b); });
    QObject::connect(session, &MoonlightSession::appListReady, this,
                     [this, id](const QStringList& ids, const QStringList& titles) {
                         emit appListReady(id, ids, titles);
                     });
    return session;
}

void MoonlightManager::pairHost(const QString& id, const QString& pin) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        emit pairingFinished(id, false);
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        emit pairingFinished(id, false);
        return;
    }
    session->pair(pin);
}

void MoonlightManager::connectHost(const QString& id, const QString& appId) {
    const auto host = hostById(id);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session == nullptr) { return; }
    // An explicit pick wins; otherwise fall back to what the user chose last.
    session->launch(appId.isEmpty() ? host->lastAppId : appId);
}

void MoonlightManager::refreshApps(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session != nullptr) { session->refreshApps(); }
}

void MoonlightManager::setHostApp(const QString& id, const QString& appId, const QString& appName) {
    auto host = hostById(id);
    if (!host.has_value()) { return; }
    host->lastAppId = appId;
    host->lastAppName = appName;
    repo_->rememberHost(*host);
    emit hostsChanged();
}

void MoonlightManager::setHostDeviceType(const QString& id, int deviceType) {
    auto host = hostById(id);
    if (!host.has_value()) { return; }
    host->deviceType = deviceType;
    repo_->rememberHost(*host);
    emit hostsChanged();
}

void MoonlightManager::disconnectHost(const QString& id) {
    if (auto* session = sessions_.value(id, nullptr)) { session->quit(); }
}

void MoonlightManager::cancelHostApp(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session != nullptr) { session->cancelHostApp(); }
}

void MoonlightManager::forgetHost(const QString& id) {
    if (auto* session = sessions_.take(id)) {
        session->quit();
        session->deleteLater();
    }
    repo_->forgetHost(id);
    emit hostsChanged();
}

std::optional<moonlight::SessionPhase> MoonlightManager::sessionPhase(const QString& id) const {
    if (auto* session = sessions_.value(id, nullptr)) { return session->phase(); }
    return std::nullopt;
}

// ── Per-slot input routing ──────────────────────────────────────────────────

void MoonlightManager::bindSlot(const QString& slotId, const QString& hostId, bool hasRumble,
                                bool hasMotion, bool hasTouchpad, bool hasBattery,
                                bool hasLightbar) {
    const auto host = hostById(hostId);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session == nullptr) { return; }

    // Re-binding the same slot elsewhere releases the old assignment first.
    unbindSlot(slotId);

    const std::string key = slotId.toStdString();
    auto& padSet = padSlots_[hostId];
    const auto number = padSet.assign(key);
    if (!number.has_value()) { return; } // host already carries kMaxPads

    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        routes_[key] = Route{session, *number, hostId};
        anyBound_.store(true, std::memory_order_relaxed);
    }

    // Announce the pad: the host's emulated-device pick decides the type it
    // materialises, the capabilities are the real hardware's.
    const std::uint8_t caps =
        moonlight::padCapabilities(hasRumble, hasMotion, hasTouchpad, hasBattery, hasLightbar);
    const std::uint8_t type = moonlight::arrivalTypeFromDevicePick(host->deviceType);
    // The whole low sixteen: every button the CONTROLLER_MULTI word can carry,
    // so the host does not have to guess which ones this pad will ever send.
    constexpr std::uint32_t kSupportedButtons = 0x0000FFFFu;
    session->sendControllerArrival(*number, type, caps, kSupportedButtons);
}

void MoonlightManager::unbindSlot(const QString& slotId) {
    const std::string key = slotId.toStdString();
    Route route;
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        const auto it = routes_.find(key);
        if (it == routes_.end()) { return; }
        route = it->second;
        routes_.erase(it);
        anyBound_.store(!routes_.empty(), std::memory_order_relaxed);
    }

    auto& padSet = padSlots_[route.hostId];
    padSet.release(key);

    // The unplug signal: one last CONTROLLER_MULTI naming this controller with
    // its bit already cleared from the active mask.
    if (route.session != nullptr) {
        moonlight::ControllerState farewell;
        farewell.controllerNumber = route.controllerNumber;
        farewell.activeGamepadMask = padSet.activeMask();
        route.session->sendControllerState(farewell);
        // And it stops being re-announced when the stream next comes up.
        route.session->forgetControllerArrival(route.controllerNumber);
    }
}

QString MoonlightManager::boundHostFor(const QString& slotId) const {
    std::lock_guard<std::mutex> lock(routeMtx_);
    const auto it = routes_.find(slotId.toStdString());
    return it == routes_.end() ? QString() : it->second.hostId;
}

QString MoonlightManager::slotForController(const QString& hostId, int controllerNumber) const {
    std::lock_guard<std::mutex> lock(routeMtx_);
    for (const auto& [slotId, route] : routes_) {
        if (route.hostId == hostId && route.controllerNumber == controllerNumber) {
            return QString::fromStdString(slotId);
        }
    }
    return {};
}

void MoonlightManager::forwardReport(const std::string& slotId, std::uint16_t buttons,
                                     std::uint8_t lt, std::uint8_t rt, std::int16_t lx,
                                     std::int16_t ly, std::int16_t rx, std::int16_t ry) {
    if (!anyBound_.load(std::memory_order_relaxed)) { return; }
    MoonlightSession* session = nullptr;
    moonlight::ControllerState state;
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        const auto it = routes_.find(slotId);
        if (it == routes_.end()) { return; }
        session = it->second.session;
        state.controllerNumber = it->second.controllerNumber;
        const auto padIt = padSlots_.constFind(it->second.hostId);
        state.activeGamepadMask = padIt == padSlots_.constEnd()
                                      ? static_cast<std::uint16_t>(1U << state.controllerNumber)
                                      : padIt->activeMask();
    }
    // The processor's button word is XUSB, which is bit-for-bit the layout
    // Moonlight's low 16 button flags use (pinned by a unit test), so the fold
    // is the identity rather than a translation table.
    state.buttonFlags = buttons;
    state.leftTrigger = lt;
    state.rightTrigger = rt;
    state.leftStickX = lx;
    state.leftStickY = ly;
    state.rightStickX = rx;
    state.rightStickY = ry;
    if (session != nullptr) { session->sendControllerState(state); }
}

void MoonlightManager::forwardMotion(const std::string& slotId, std::int16_t gyroX,
                                     std::int16_t gyroY, std::int16_t gyroZ, std::int16_t accelX,
                                     std::int16_t accelY, std::int16_t accelZ) {
    if (!anyBound_.load(std::memory_order_relaxed)) { return; }
    MoonlightSession* session = nullptr;
    std::uint8_t number = 0;
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        const auto it = routes_.find(slotId);
        if (it == routes_.end()) { return; }
        session = it->second.session;
        number = it->second.controllerNumber;
    }
    if (session == nullptr) { return; }
    // The wire carries floats in the sensors' natural units; the SDL path hands
    // us the satellite's fixed-point scaling (gyro 2000/32767 deg/s, accel
    // 4/32767 g), so convert once here.
    constexpr float kGyroScale = 2000.0F / 32767.0F;
    constexpr float kAccelScale = 4.0F / 32767.0F;
    session->sendControllerMotion(
        number, moonlight::kMotionGyro, static_cast<float>(gyroX) * kGyroScale,
        static_cast<float>(gyroY) * kGyroScale, static_cast<float>(gyroZ) * kGyroScale);
    session->sendControllerMotion(
        number, moonlight::kMotionAccel, static_cast<float>(accelX) * kAccelScale,
        static_cast<float>(accelY) * kAccelScale, static_cast<float>(accelZ) * kAccelScale);
}

void MoonlightManager::forwardBattery(const std::string& slotId, std::uint8_t level,
                                      std::uint8_t satelliteStatus) {
    if (!anyBound_.load(std::memory_order_relaxed)) { return; }
    MoonlightSession* session = nullptr;
    std::uint8_t number = 0;
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        const auto it = routes_.find(slotId);
        if (it == routes_.end()) { return; }
        session = it->second.session;
        number = it->second.controllerNumber;
    }
    if (session == nullptr) { return; }
    session->sendControllerBattery(
        number, moonlight::batteryStateFromSatelliteStatus(satelliteStatus), level);
}

} // namespace dish::net
