// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ConnectionHub.h"

#include <QSet>

#include <algorithm>

namespace dish::net {

ConnectionHub::ConnectionHub(WifiConnectionManager* wifi, ConnectionStore* store, QObject* parent)
    : QObject(parent), wifi_(wifi), store_(store) {
    QObject::connect(wifi_, &WifiConnectionManager::poolChanged, this, &ConnectionHub::rebuild);
    // Discovery hits split an Idle paired entry into Ready (visible on the
    // network) vs Saved (not in the current scan), so a fresh scan must
    // re-derive every row's LinkState.
    QObject::connect(wifi_, &WifiConnectionManager::discoveredChanged, this,
                     &ConnectionHub::rebuild);
    // A controller-add the server rejected leaves a phantom local binding —
    // the slot card would claim it is bound while the satellite has no
    // controller for it. Roll the binding back so the UI reflects reality.
    QObject::connect(wifi_, &WifiConnectionManager::slotRegistrationFailed, this,
                     &ConnectionHub::unbind);
    rebuild();
}

void ConnectionHub::rebuild() {
    QHash<QString, models::RememberedWifi> remembered;
    for (const auto& r : store_->remembered()) { remembered.insert(r.id, r); }

    // Set of currently-discovered satellite ids. Used to split an Idle paired
    // entry into LinkState::Ready (we see it on the network) vs
    // LinkState::Saved (we don't). Mirrors dish-android's discoveredIdSet.
    QSet<QString> discoveredIds;
    for (const auto& s : wifi_->discoveredServers()) { discoveredIds.insert(s.id()); }

    QSet<QString> ids;
    for (auto it = wifi_->connections().begin(); it != wifi_->connections().end(); ++it) {
        ids.insert(it.key());
    }
    for (auto it = remembered.begin(); it != remembered.end(); ++it) { ids.insert(it.key()); }

    QList<models::ConnectionSummary> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        auto* conn = wifi_->get(id);
        const models::DiscoveredServer server =
            (conn != nullptr) ? conn->server() : remembered.value(id).toDiscovered();
        if (!server.isValid()) { continue; }
        // Derive LinkState from the wire-level SessionState plus whether this
        // id is currently in the discovery set. Mirrors dish-android's
        // ConnectionHub.buildSatelliteSummary.
        //
        // - SessionState::Live      -> LinkState::Connected
        // - SessionState::Linking   -> LinkState::Connecting
        // - SessionState::Faltering -> LinkState::Unstable (not yet reachable --
        //   native exposes only binary alive)
        // - SessionState::Stale     -> LinkState::Stale (post-onDead silent
        //   recovery path; row chip reads "Needs pairing" until the retry
        //   lands a Live session or the user kicks a fresh pair)
        // - SessionState::Idle / null:
        //     in discoveredIds      -> LinkState::Ready
        //     not in discoveredIds  -> LinkState::Saved
        // Baseline default; every branch below reassigns it. The explicit
        // initializer silences MSVC C4701 (the switch is exhaustive but has no
        // default, so the compiler can't prove every path assigns).
        models::LinkState live = models::LinkState::Saved;
        if (conn != nullptr) {
            switch (conn->state()) {
            case SessionState::Live:
                live = models::LinkState::Connected;
                break;
            case SessionState::Linking:
                live = models::LinkState::Connecting;
                break;
            case SessionState::Faltering:
                // Entered by WifiConnection::onAliveTick at >=2 consecutive
                // missed acks; recovers to Live on the next ack.
                live = models::LinkState::Unstable;
                break;
            case SessionState::Stale:
                // Silent-recovery marker: heartbeat stream dropped or an
                // auto-reconnect's pair came back AuthRequired. The row chip
                // says "Needs pairing"; the manager keeps retrying silently.
                live = models::LinkState::Stale;
                break;
            case SessionState::Idle:
                live = discoveredIds.contains(id) ? models::LinkState::Ready
                                                  : models::LinkState::Saved;
                break;
            }
        } else {
            live = discoveredIds.contains(id) ? models::LinkState::Ready : models::LinkState::Saved;
        }
        std::optional<QString> bound;
        for (auto it = bindings_.begin(); it != bindings_.end(); ++it) {
            if (it.value() == id) {
                bound = it.key();
                break;
            }
        }
        const QString label = server.name.isEmpty() ? server.ip : server.name;
        models::ConnectionSummary s;
        s.id = id;
        s.label = label;
        s.detail = QStringLiteral("%1 \u2022 UDP %2").arg(server.ip).arg(server.udpPort);
        s.live = live;
        s.boundSlotId = bound;
        out.append(s);
    }
    std::sort(out.begin(), out.end(),
              [](const auto& a, const auto& b) { return a.label < b.label; });
    summaries_ = std::move(out);
    emit changed();
}

std::optional<models::ConnectionSummary> ConnectionHub::summary(const QString& id) const {
    for (const auto& s : summaries_) {
        if (s.id == id) { return s; }
    }
    return std::nullopt;
}

ConnectionHub::ReportSender ConnectionHub::reportSenderForSlot(const QString& slotId) const {
    const auto cid = bindings_.value(slotId);
    if (cid.isEmpty()) { return {}; }
    auto* conn = wifi_->get(cid);
    if (conn == nullptr) { return {}; }
    // Capture by raw pointer — WifiConnection is parented to the manager and
    // outlives the input thread (Manager dtor disconnects all sessions before
    // destruction). The internal ClientRef provides per-call thread safety.
    return [conn](std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt, std::int16_t lx,
                  std::int16_t ly, std::int16_t rx,
                  std::int16_t ry) { conn->sendReport(buttons, lt, rt, lx, ly, rx, ry); };
}

ConnectionHub::MotionSender ConnectionHub::motionSenderForSlot(const QString& slotId) const {
    const auto cid = bindings_.value(slotId);
    if (cid.isEmpty()) { return {}; }
    auto* conn = wifi_->get(cid);
    if (conn == nullptr) { return {}; }
    return [conn](std::int16_t gx, std::int16_t gy, std::int16_t gz, std::int16_t ax,
                  std::int16_t ay, std::int16_t az,
                  std::uint32_t dtUs) { conn->sendMotion(gx, gy, gz, ax, ay, az, dtUs); };
}

ConnectionHub::BatterySender ConnectionHub::batterySenderForSlot(const QString& slotId) const {
    const auto cid = bindings_.value(slotId);
    if (cid.isEmpty()) { return {}; }
    auto* conn = wifi_->get(cid);
    if (conn == nullptr) { return {}; }
    return [conn](std::uint8_t level, std::uint8_t status) { conn->sendBattery(level, status); };
}

ConnectionHub::TouchpadSender ConnectionHub::touchpadSenderForSlot(const QString& slotId) const {
    const auto cid = bindings_.value(slotId);
    if (cid.isEmpty()) { return {}; }
    auto* conn = wifi_->get(cid);
    if (conn == nullptr) { return {}; }
    return [conn](bool f0a, std::uint8_t f0id, std::int16_t f0x, std::int16_t f0y, bool f1a,
                  std::uint8_t f1id, std::int16_t f1x, std::int16_t f1y, bool button,
                  std::uint32_t eventTimeMs) {
        conn->sendTouchpad(f0a, f0id, f0x, f0y, f1a, f1id, f1x, f1y, button, eventTimeMs);
    };
}

void ConnectionHub::bind(const QString& slotId, const QString& connectionId) {
    QHash<QString, QString> current = bindings_;
    QString priorSlot;
    for (auto it = current.begin(); it != current.end(); ++it) {
        if (it.value() == connectionId && it.key() != slotId) {
            priorSlot = it.key();
            break;
        }
    }
    if (!priorSlot.isEmpty()) {
        current.remove(priorSlot);
        if (auto* prior = wifi_->get(connectionId)) { prior->detachSlot(); }
    }
    current.insert(slotId, connectionId);
    bindings_ = current;
    rebuild();
    // Resolve the bound pad's hardware so the controller-add advertises the
    // right type + capabilities. Each predicate is absent in tests / before
    // the bridge is wired — fall back to "Xbox, no lightbar, no motion".
    const bool hasLightbar = lightbarCapabilityFn_ && lightbarCapabilityFn_(slotId);
    const bool hasMotion = motionCapabilityFn_ && motionCapabilityFn_(slotId);
    const int controllerType = controllerTypeFn_ ? controllerTypeFn_(slotId) : 0;
    if (auto* c = wifi_->get(connectionId)) {
        c->attachSlot(slotId, controllerType, hasLightbar, hasMotion);
    }
}

void ConnectionHub::unbind(const QString& slotId) {
    if (!bindings_.contains(slotId)) { return; }
    const auto cid = bindings_.take(slotId);
    if (auto* c = wifi_->get(cid)) { c->detachSlot(); }
    rebuild();
}

std::optional<models::ConnectionSummary>
ConnectionHub::boundConnection(const QString& slotId) const {
    const auto cid = bindings_.value(slotId);
    if (cid.isEmpty()) { return std::nullopt; }
    return summary(cid);
}

} // namespace dish::net
