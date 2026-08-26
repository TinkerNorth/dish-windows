// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightManager.h"

#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"
#include "source/connection/NvstreamDiscovery.h"

#include <QLoggingCategory>
#include <QRandomGenerator>
#include <QSet>
#include <QSettings>

namespace dish::net {

// Every command below is something a user pressed, so every path out of one says
// what happened. A command that returns without a word is undiagnosable from a
// log and invisible on screen, which is the same defect twice.
Q_LOGGING_CATEGORY(lcMoonlightManager, "dish.moonlight.manager")

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

QList<models::MoonlightHost> mergeDiscoverySweep(const QList<models::MoonlightHost>& previous,
                                                 const QList<models::MoonlightHost>& found) {
    // Deliberately not a union. A sweep that answered is the truth about what is
    // on the network right now, so a host that really went away does leave the
    // list; only the answer that says nothing at all is refused.
    if (found.isEmpty()) { return previous; }
    return found;
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
            this, [this, found] { applyDiscoverySweep(found); }, Qt::QueuedConnection);
    });
}

void MoonlightManager::applyDiscoverySweep(const QList<models::MoonlightHost>& found) {
    if (found.isEmpty() && !discovered_.isEmpty()) {
        qCWarning(lcMoonlightManager)
            << "scan: found nothing, keeping the" << discovered_.size()
            << "host(s) the last sweep found. An empty answer is not a network with no"
            << "hosts on it, and a host that leaves this list is one no binding can name.";
    }
    discovered_ = mergeDiscoverySweep(discovered_, found);
    scanning_ = false;
    qCInfo(lcMoonlightManager) << "scan: finished with" << discovered_.size()
                               << "host(s) in the discovered list";
    emit scanningChanged();
    emit hostsChanged();
}

void MoonlightManager::addManualHost(const QString& ip, const QString& name) {
    if (ip.isEmpty()) {
        qCWarning(lcMoonlightManager) << "add by address: refused an empty address";
        return;
    }
    models::MoonlightHost h;
    h.ip = ip;
    h.name = name.isEmpty() ? ip : name;
    qCInfo(lcMoonlightManager) << "add by address:" << h.id() << "remembered";
    repo_->rememberHost(h);
    emit hostsChanged();
}

std::optional<models::MoonlightHost> MoonlightManager::hostById(const QString& id) const {
    if (const auto remembered = rememberedHost(id)) { return remembered; }
    for (const auto& h : discovered_) {
        if (h.id() == id) { return h; }
    }
    return std::nullopt;
}

std::optional<models::MoonlightHost> MoonlightManager::rememberedHost(const QString& id) const {
    for (const auto& h : repo_->hosts()) {
        if (h.id() == id) { return h; }
    }
    return std::nullopt;
}

void MoonlightManager::rememberProvenTrust(const QString& id) {
    // CONFIRMING TRUST IS A PAIRING OUTCOME. A host that already holds our
    // certificate needs no PIN, but it must leave the client exactly where a
    // fresh five-phase pairing would: the record written and the row paired. A
    // client that only reports the trust it just proved can never write a host
    // down again once it has forgotten one, and every binding naming that host is
    // then one missed discovery sweep away from having no host at all.
    //
    // Only a MUTUAL-TLS answer counts. The plaintext PairStatus cannot: this
    // client sends the same fixed uniqueid every install does, so a 1 there says
    // some client with that id is paired and not that this one is.
    auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "trust:" << id << "proved itself but resolves to no host";
        return;
    }
    if (host->paired && rememberedHost(id).has_value()) { return; }
    host->paired = true;
    repo_->rememberHost(*host);
    qCInfo(lcMoonlightManager) << "trust:" << id
                               << "answered a mutual-TLS call, so its pairing is proven"
                               << "and is now remembered";
    emit hostsChanged();
}

MoonlightSession* MoonlightManager::ensureSession(const models::MoonlightHost& host) {
    const QString id = host.id();
    if (auto* existing = sessions_.value(id, nullptr)) { return existing; }
    if (!identity_.has_value()) {
        identity_ = repo_->getOrCreateIdentity();
        if (!identity_.has_value()) {
            // The one failure that makes every Moonlight command impossible, and
            // the one nobody would ever guess at from the outside.
            qCCritical(lcMoonlightManager)
                << "no client identity: generating or loading the Moonlight certificate failed."
                << "Nothing can pair, probe or bind until that succeeds.";
            return nullptr;
        }
    }
    auto* session = new MoonlightSession(host, *identity_, repo_.get(), this);
    sessions_.insert(id, session);
    QObject::connect(session, &MoonlightSession::phaseChanged, this, [this, id, session] {
        // A host that accepted our /launch ran its verify callback against our
        // certificate to do it, so reaching the handshake is the same proof the
        // app list gives and the second path a trust can be established on
        // without anyone typing a PIN.
        if (session->phase() == moonlight::SessionPhase::RtspHandshake) { rememberProvenTrust(id); }
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
            {
                auto& probe = probes_[id];
                probe.appsInFlight = false;
                probe.appsFetched = ok;
                probe.appsFailed = !ok;
                probe.appCount = ok ? static_cast<int>(ids.size()) : 0;
                probe.unauthorized = unauthorized;
                if (ok) {
                    // A readable mutual-TLS reply IS the proof of trust: the host
                    // ran its verify callback and let us in, whatever a plaintext
                    // probe said about PairStatus.
                    probe.answered = true;
                    probe.timedOut = false;
                    probe.mtlsVerified = true;
                }
                if (unauthorized) { probe.mtlsVerified = false; }
            }
            // Scoped above and called here: this emits, and a handler that probes
            // some other host would insert into probes_ and leave a reference into
            // it dangling mid-update.
            if (ok) { rememberProvenTrust(id); }
            emit appListReady(id, ids, titles);
            emit hostsChanged();
        });
    QObject::connect(
        session, &MoonlightSession::probeFinished, this,
        [this, id](bool answered, bool pairStatus, const QString& uniqueId) {
            auto& probe = probes_[id];
            probe.inFlight = false;
            probe.answered = answered;
            probe.timedOut = !answered;
            probe.pairStatus = pairStatus;
            // A host that came back with a different identity is a
            // different host: the old pairing cannot work and the
            // user has to be told rather than shown a failure later.
            const auto known = rememberedHost(id);
            probe.uniqueIdChanged = answered && !uniqueId.isEmpty() && known.has_value() &&
                                    !known->uuid.isEmpty() && known->uuid != uniqueId;
            if (probe.uniqueIdChanged) {
                qCWarning(lcMoonlightManager)
                    << id << "answered with uniqueid" << uniqueId << "but was remembered as"
                    << known->uuid << ": this is a different host and the old pairing is dead";
            }
            // Learned once and kept, or the comparison above has
            // nothing to compare against and M8 can never render.
            // It is a witness and NOT the id: see MoonlightHost::id.
            if (answered && !uniqueId.isEmpty() && known.has_value() && known->uuid.isEmpty()) {
                auto learned = *known;
                learned.uuid = uniqueId;
                repo_->rememberHost(learned);
                qCInfo(lcMoonlightManager) << id << "identified itself as" << uniqueId;
            }
            emit hostsChanged();
        });
    return session;
}

void MoonlightManager::pairHost(const QString& id, const QString& pin) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager)
            << "pair:" << id << "resolves to no host, remembered or found";
        emit pairingFinished(id, false);
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        qCWarning(lcMoonlightManager) << "pair:" << id << "has no session to pair through";
        emit pairingFinished(id, false);
        return;
    }
    // Read, never held: cancelPairing and pair below both emit, and a handler that
    // probes some other host would insert into probes_ and invalidate a reference
    // taken across the call.
    const bool wasPairing = probes_[id].pairingActive;
    const bool identityMoved = probes_[id].uniqueIdChanged || session->serverCertMismatch();

    // "New code" is pressed while a pairing is already parked on the host waiting
    // for a PIN nobody is going to type. Racing a second exchange against it would
    // leave two chains reporting into one row.
    if (wasPairing) {
        qCInfo(lcMoonlightManager)
            << "pair:" << id << "restarting; the previous attempt is dropped";
        session->cancelPairing();
    }
    // A HOST THAT ANNOUNCED A NEW IDENTITY HAS ALREADY SUPPLIED THE EVIDENCE. The
    // pin we hold belongs to the host it replaced, so keeping it protects nothing
    // and refuses phase 5 before the host ever answers, which leaves the user with
    // no way back in from inside the app. Asking to pair again IS the decision to
    // trust what this host presents next.
    if (identityMoved) {
        qCWarning(lcMoonlightManager)
            << "pair:" << id << "announced an identity that is not the remembered one;"
            << "dropping the pinned certificate so this pairing can pin the new one";
        repo_->clearServerCert(id);
        session->clearServerCertMismatch();
    }
    qCInfo(lcMoonlightManager) << "pair:" << id << "starting the five HTTP phases";
    {
        auto& probe = probes_[id];
        probe.uniqueIdChanged = probe.uniqueIdChanged && !identityMoved;
        probe.pairingActive = true;
        probe.pairingRefused = false;
    }
    session->pair(pin);
    emit hostsChanged();
}

void MoonlightManager::cancelPairing(const QString& id) {
    auto* session = sessions_.value(id, nullptr);
    if (session == nullptr) {
        qCDebug(lcMoonlightManager) << "cancel pairing:" << id << "carries no session";
        return;
    }
    qCInfo(lcMoonlightManager) << "cancel pairing:" << id;
    session->cancelPairing();
    // The section renders from these, and a cancel is not a refusal: clearing both
    // drops it back to whatever the last probe said about this host.
    auto& probe = probes_[id];
    probe.pairingActive = false;
    probe.pairingRefused = false;
    emit hostsChanged();
}

void MoonlightManager::probeHost(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "probe:" << id << "resolves to no host";
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        qCWarning(lcMoonlightManager) << "probe:" << id << "has no session to probe through";
        return;
    }
    auto& probe = probes_[id];
    if (probe.inFlight) {
        qCDebug(lcMoonlightManager) << "probe:" << id << "already in flight, not asking twice";
        return;
    }
    probe.inFlight = true;
    probe.answered = false;
    probe.timedOut = false;
    session->probe();
    emit hostsChanged();
}

void MoonlightManager::connectHost(const QString& id, const QString& appId) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "connect:" << id << "resolves to no host";
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        qCWarning(lcMoonlightManager) << "connect:" << id << "has no session to launch on";
        return;
    }
    // An explicit pick wins; otherwise fall back to what the user chose last.
    session->launch(appId.isEmpty() ? host->lastAppId : appId);
}

void MoonlightManager::refreshApps(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "app list:" << id << "resolves to no host";
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        qCWarning(lcMoonlightManager) << "app list:" << id << "has no session to ask through";
        return;
    }
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
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "app pick:" << id << "resolves to no host; not remembered";
        return;
    }
    host->lastAppId = appId;
    host->lastAppName = appName;
    repo_->rememberHost(*host);
    emit hostsChanged();
}

void MoonlightManager::setHostDeviceType(const QString& id, int deviceType) {
    auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager)
            << "type pick:" << id << "resolves to no host; not remembered";
        return;
    }
    host->deviceType = deviceType;
    repo_->rememberHost(*host);
    emit hostsChanged();
}

void MoonlightManager::disconnectHost(const QString& id) {
    if (auto* session = sessions_.value(id, nullptr)) {
        qCInfo(lcMoonlightManager) << "disconnect:" << id;
        session->quit();
        return;
    }
    qCDebug(lcMoonlightManager) << "disconnect:" << id
                                << "carries no session; nothing to tear down";
}

void MoonlightManager::cancelHostApp(const QString& id) {
    const auto host = hostById(id);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager) << "quit session:" << id << "resolves to no host";
        return;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) {
        qCWarning(lcMoonlightManager) << "quit session:" << id << "has no session to ask through";
        return;
    }
    qCInfo(lcMoonlightManager) << "quit session:" << id << "asking the host to close its app";
    session->cancelHostApp();
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
    qCInfo(lcMoonlightManager) << "forget:" << id << "dropping every piece of state it owns";
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
        // A pairing parked on the host is waiting for a PIN for a host that will
        // not exist when it lands. Cancelling it here is what stops phase 5
        // completing into a forget and writing the pairing back.
        session->cancelPairing();
        // BEFORE the quit, because quit() tears the session down and the teardown
        // sends /cancel over TLS. That handshake runs the session's pin verifier
        // on a LATER turn of the event loop, which would write the pinned
        // certificate straight back into the store the forget below just cleared,
        // and leave a forgotten host holding the one piece of state that decides
        // whether the NEXT pairing is allowed to succeed.
        session->detachFromStore();
        // AND NOTHING THIS SESSION SAYS REACHES US AGAIN. Its replies land on
        // handlers that write probes_[id], and QHash::operator[] INSERTS: a reply
        // that arrives between here and the deferred delete would re-create the
        // cache for a host that no longer exists, and the app-list handler would
        // go on to remember its pairing as proved. deleteLater is not soon enough
        // to rely on, so the wire is cut rather than raced.
        QObject::disconnect(session, nullptr, this, nullptr);
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

moonlight::BindOutcome MoonlightManager::bindSlot(const QString& slotId, const QString& hostId,
                                                  int controllerType, bool hasRumble,
                                                  bool hasMotion, bool hasTouchpad, bool hasBattery,
                                                  bool hasLightbar) {
    const auto host = hostById(hostId);
    if (!host.has_value()) {
        qCWarning(lcMoonlightManager)
            << "bind:" << slotId << "names host" << hostId
            << "which resolves to nothing. The binding stays on file and takes effect"
            << "the next time that host is remembered or found.";
        return moonlight::BindOutcome::UnknownHost;
    }
    auto* session = ensureSession(*host);
    if (session == nullptr) { return moonlight::BindOutcome::NoIdentity; }

    // Re-binding the same slot elsewhere releases the old assignment first.
    unbindSlot(slotId);

    const std::string key = slotId.toStdString();
    auto& padSet = padSlots_[hostId];
    // Read before the assignment: whether this pad has to bring the session up is
    // a question about the host as it was, not as it is about to be.
    const bool firstOnHost = moonlight::bindStartsSession(padSet);
    const auto number = padSet.assign(key);
    if (!number.has_value()) {
        qCWarning(lcMoonlightManager)
            << "bind:" << hostId << "already carries" << static_cast<int>(moonlight::kMaxPads)
            << "controllers, which is the protocol ceiling." << slotId << "was not routed.";
        return moonlight::BindOutcome::HostFull;
    }

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

    qCInfo(lcMoonlightManager) << "bind:" << slotId << "->" << hostId << "as controller"
                               << static_cast<int>(*number) << "type" << static_cast<int>(type)
                               << (firstOnHost ? "(starting the session)" : "(joining a session)");

    // The arrival is remembered and replayed when the stream comes up, so the
    // first pad may announce itself before there is a stream to announce on. A
    // later pad joins the session that is already running and sends no HTTP at
    // all: launch() reduces to nothing outside a phase that can start one.
    if (firstOnHost) { session->launch(host->lastAppId); }
    return moonlight::BindOutcome::Bound;
}

void MoonlightManager::unbindSlot(const QString& slotId) {
    const std::string key = slotId.toStdString();
    Route route;
    {
        std::lock_guard<std::mutex> lock(routeMtx_);
        const auto it = routes_.find(key);
        if (it == routes_.end()) {
            qCDebug(lcMoonlightManager)
                << "unbind:" << slotId << "was not routed at a Moonlight host";
            return;
        }
        route = it->second;
        routes_.erase(it);
        anyBound_.store(!routes_.empty(), std::memory_order_relaxed);
    }

    auto& padSet = padSlots_[route.hostId];
    padSet.release(key);
    qCInfo(lcMoonlightManager) << "unbind:" << slotId << "left" << route.hostId << "as controller"
                               << static_cast<int>(route.controllerNumber) << "leaving"
                               << static_cast<int>(padSet.size()) << "on the host";

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
    if (!binding.isValid()) {
        qCWarning(lcMoonlightManager) << "binding: refused a record naming slot" << binding.slotId
                                      << "and host" << binding.hostId << "; both are required";
        return;
    }
    // THE HOST GOES WITH IT. A host that only exists in the discovery sweep is
    // gone the moment this process ends, so the binding would come back to an id
    // nothing resolves and sit dormant until the next scan happened to find that
    // host again. Pairing is NOT the condition: a binding is a durable intent and
    // a host nobody has paired yet is a perfectly good thing to intend to drive.
    if (!rememberedHost(binding.hostId).has_value()) {
        if (const auto host = hostById(binding.hostId)) {
            qCInfo(lcMoonlightManager)
                << "binding: remembering host" << binding.hostId << "because a binding names it";
            repo_->rememberHost(*host);
        } else {
            qCWarning(lcMoonlightManager)
                << "binding:" << binding.slotId << "names host" << binding.hostId
                << "which resolves to nothing, so it cannot be remembered with the binding";
        }
    }
    qCInfo(lcMoonlightManager) << "binding:" << binding.slotId << "->" << binding.hostId << "type"
                               << binding.controllerType << "remembered";
    repo_->rememberBinding(binding);
    bindings_ = repo_->bindings();
    emit hostsChanged();
}

void MoonlightManager::forgetBinding(const QString& slotId) {
    if (!binding(slotId).has_value()) {
        qCDebug(lcMoonlightManager) << "binding:" << slotId << "had none to forget";
        return;
    }
    qCInfo(lcMoonlightManager) << "binding:" << slotId << "forgotten";
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
        in.serverCertChanged = session->serverCertMismatch();
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
