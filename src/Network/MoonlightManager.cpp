// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightManager.h"

#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"
#include "source/connection/NvstreamDiscovery.h"

#include <QRandomGenerator>
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

QString generateMoonlightPin() {
    // Four digits, uniform over 0000..9999, from the cryptographically seeded
    // generator rather than the arithmetic one.
    return QStringLiteral("%1").arg(QRandomGenerator::global()->bounded(10000), 4, 10,
                                    QLatin1Char('0'));
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
      repo_(std::make_unique<repository::MoonlightHostRepository>(std::move(settings))) {
    bindings_ = repo_->bindings();
}

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
        auto& probe = probes_[id];
        probe.pairingActive = false;
        probe.pairingRefused = !ok;
        if (ok) {
            // The pairing that just landed is proof of trust on its own; nothing
            // has to ask the host again to draw it.
            probe.answered = true;
            probe.timedOut = false;
            probe.mtlsVerified = true;
            probe.unauthorized = false;
            probe.uniqueIdChanged = false;
        }
        emit pairingFinished(id, ok);
        emit hostsChanged();
    });
    QObject::connect(session, &MoonlightSession::rumbleReceived, this,
                     [this, id](int n, int lo, int hi) { emit rumbleReceived(id, n, lo, hi); });
    QObject::connect(
        session, &MoonlightSession::rgbLedReceived, this,
        [this, id](int n, int r, int g, int b) { emit rgbLedReceived(id, n, r, g, b); });
    QObject::connect(
        session, &MoonlightSession::appListReady, this,
        [this, id](const QStringList& ids, const QStringList& titles, bool ok, bool unauthorized) {
            auto& probe = probes_[id];
            probe.appsInFlight = false;
            probe.appsFetched = ok;
            probe.appsFailed = !ok;
            probe.appCount = ok ? static_cast<int>(ids.size()) : 0;
            probe.unauthorized = unauthorized;
            if (ok) {
                // A readable mutual-TLS reply IS the proof of trust: the host ran
                // its verify callback and let us in, whatever a plaintext probe
                // said about PairStatus.
                probe.answered = true;
                probe.timedOut = false;
                probe.mtlsVerified = true;
            }
            if (unauthorized) { probe.mtlsVerified = false; }
            emit appListReady(id, ids, titles);
            emit hostsChanged();
        });
    QObject::connect(session, &MoonlightSession::probeFinished, this,
                     [this, id](bool answered, bool pairStatus, const QString& uniqueId) {
                         auto& probe = probes_[id];
                         probe.inFlight = false;
                         probe.answered = answered;
                         probe.timedOut = !answered;
                         probe.pairStatus = pairStatus;
                         // A host that came back with a different identity is a
                         // different host: the old pairing cannot work and the
                         // user has to be told rather than shown a failure later.
                         const auto known = hostById(id);
                         probe.uniqueIdChanged = answered && !uniqueId.isEmpty() &&
                                                 known.has_value() && !known->uuid.isEmpty() &&
                                                 known->uuid != uniqueId;
                         emit hostsChanged();
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
    auto& probe = probes_[id];
    probe.pairingActive = true;
    probe.pairingRefused = false;
    session->pair(pin);
    emit hostsChanged();
}

void MoonlightManager::probeHost(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session == nullptr) { return; }
    auto& probe = probes_[id];
    if (probe.inFlight) { return; }
    probe.inFlight = true;
    probe.answered = false;
    probe.timedOut = false;
    session->probe();
    emit hostsChanged();
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
    if (session == nullptr) { return; }
    auto& probe = probes_[id];
    probe.appsInFlight = true;
    probe.appsFetched = false;
    probe.appsFailed = false;
    probe.unauthorized = false;
    session->refreshApps();
    emit hostsChanged();
}

QString MoonlightManager::runningAppName(const QString& id) const {
    const auto host = hostById(id);
    return host.has_value() ? host->lastAppName : QString();
}

QString MoonlightManager::refusalMessage(const QString& id) const {
    auto* session = sessions_.value(id, nullptr);
    return session == nullptr ? QString() : session->failureMessage();
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

QStringList MoonlightManager::slotsRoutedTo(const QString& hostId) const {
    std::lock_guard<std::mutex> lock(routeMtx_);
    QStringList out;
    for (const auto& [slotId, route] : routes_) {
        if (route.hostId == hostId) { out.append(QString::fromStdString(slotId)); }
    }
    return out;
}

void MoonlightManager::forgetHost(const QString& id) {
    // EVERY ROUTE AT THIS HOST GOES FIRST. forwardReport reads the routing table
    // on the input thread and dereferences the session it finds there, so a route
    // left pointing at a session this function is about to delete is a use after
    // free on that thread. unbindSlot also sends each pad its farewell and tears
    // the session down once the last one is off.
    for (const auto& slotId : slotsRoutedTo(id)) { unbindSlot(slotId); }
    padSlots_.remove(id);
    // What we learned by asking this host goes with it: a host forgotten and
    // added again is a stranger, not a paired one.
    probes_.remove(id);
    // A binding is an intent to drive THIS host. Forgetting the host retires it,
    // or it would keep asking to be re-attached to a pairing that is gone.
    repo_->forgetBindingsForHost(id);
    bindings_ = repo_->bindings();

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

void MoonlightManager::bindSlot(const QString& slotId, const QString& hostId, int controllerType,
                                bool hasRumble, bool hasMotion, bool hasTouchpad, bool hasBattery,
                                bool hasLightbar) {
    const auto host = hostById(hostId);
    if (!host.has_value()) { return; }
    auto* session = ensureSession(*host);
    if (session == nullptr) { return; }

    // Re-binding the same slot elsewhere releases the old assignment first.
    unbindSlot(slotId);

    const std::string key = slotId.toStdString();
    auto& padSet = padSlots_[hostId];
    // Read before the assignment: whether this pad has to bring the session up is
    // a question about the host as it was, not as it is about to be.
    const bool firstOnHost = moonlight::bindStartsSession(padSet);
    const auto number = padSet.assign(key);
    if (!number.has_value()) { return; } // host already carries kMaxPads

    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        routes_[key] = Route{session, *number, hostId};
        anyBound_.store(true, std::memory_order_relaxed);
    }

    // Announce the pad. The type is THIS BINDING's pick with Auto already
    // resolved, and the capabilities are that type's ceiling intersected with the
    // pad's real hardware, so the host is never told about something that will
    // not arrive.
    const std::uint8_t type = moonlight::arrivalTypeForBinding(controllerType, hasMotion);
    const std::uint8_t caps = moonlight::declaredCapabilities(type, hasRumble, hasMotion,
                                                              hasTouchpad, hasBattery, hasLightbar);
    session->sendControllerArrival(*number, type, caps, moonlight::declaredButtonFlags(caps));

    // The arrival is remembered and replayed when the stream comes up, so the
    // first pad may announce itself before there is a stream to announce on. A
    // later pad joins the session that is already running and sends no HTTP at
    // all: launch() reduces to nothing outside a phase that can start one.
    if (firstOnHost) { session->launch(host->lastAppId); }
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
        // The last pad off the host owns the teardown: a session nobody is bound
        // to is an app stranded on someone's desktop, and it is also what refuses
        // the next /launch.
        if (moonlight::unbindEndsSession(padSet)) { route.session->quit(); }
    }
}

int MoonlightManager::boundSlotCount(const QString& hostId) const {
    const auto it = padSlots_.constFind(hostId);
    return it == padSlots_.constEnd() ? 0 : static_cast<int>(it->size());
}

QList<models::MoonlightBinding> MoonlightManager::bindings() const { return bindings_; }

std::optional<models::MoonlightBinding> MoonlightManager::binding(const QString& slotId) const {
    for (const auto& b : bindings_) {
        if (b.slotId == slotId) { return b; }
    }
    return std::nullopt;
}

void MoonlightManager::rememberBinding(const models::MoonlightBinding& binding) {
    if (!binding.isValid()) { return; }
    repo_->rememberBinding(binding);
    bindings_ = repo_->bindings();
    emit hostsChanged();
}

void MoonlightManager::forgetBinding(const QString& slotId) {
    if (!binding(slotId).has_value()) { return; }
    repo_->forgetBinding(slotId);
    bindings_ = repo_->bindings();
    emit hostsChanged();
}

moonlight::SessionUiInputs MoonlightManager::sessionUiInputs(const QString& hostId,
                                                             const QString& slotId) const {
    moonlight::SessionUiInputs in;
    const auto probe = probes_.value(hostId);
    in.probeInFlight = probe.inFlight;
    in.probeAnswered = probe.answered;
    in.probeTimedOut = probe.timedOut;
    in.hostPairStatus = probe.pairStatus || probe.mtlsVerified;
    in.uniqueIdChanged = probe.uniqueIdChanged;
    in.pairingActive = probe.pairingActive;
    in.pairingRefused = probe.pairingRefused;
    in.appsInFlight = probe.appsInFlight;
    in.appsFetched = probe.appsFetched;
    in.appsFailed = probe.appsFailed;
    in.appCount = probe.appCount;
    in.serverCertStored = repo_->serverCert(hostId).has_value();
    in.boundControllers = boundSlotCount(hostId);
    // A host that answers the plaintext probe but hands the mutual-TLS list back
    // a 401 has forgotten this device whatever its PairStatus said.
    in.unauthorized = probe.unauthorized;

    if (auto* session = sessions_.value(hostId, nullptr)) {
        const moonlight::SessionState state{session->phase(), session->failure()};
        in.outcome = moonlight::sessionOutcomeFor(state);
        in.sessionLive = in.outcome == moonlight::SessionOutcome::Live;
        // This binding rides that session only once its pad is actually routed
        // to this host; until then it is a binding that WOULD join one.
        in.bindingLive = in.sessionLive && !slotId.isEmpty() && boundHostFor(slotId) == hostId;
    }
    return in;
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
