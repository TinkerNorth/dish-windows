// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/ConnectionCoordinator.h"

#include <algorithm>

namespace dish::composer {

namespace {

// Map the wire-level net::SessionState onto the Qt-free presence enum the pure
// mappers consume. One-to-one; kept at the Qt boundary so the composer/reducers
// carry no Network dependency.
reducer::SessionPresence presenceOf(net::SessionState state) {
    switch (state) {
    case net::SessionState::Idle:
        return reducer::SessionPresence::Idle;
    case net::SessionState::Linking:
        return reducer::SessionPresence::Linking;
    case net::SessionState::Live:
        return reducer::SessionPresence::Live;
    case net::SessionState::Faltering:
        return reducer::SessionPresence::Faltering;
    case net::SessionState::Stale:
    default:
        return reducer::SessionPresence::Stale;
    }
}

} // namespace

ConnectionCoordinator::ConnectionCoordinator(net::WifiConnectionManager* wifi,
                                             net::ConnectionHub* hub, QObject* parent)
    : QObject(parent), wifi_(wifi), hub_(hub), sessions_({}), remembered_({}), discoveredIds_({}),
      bindings_({}), staleIds_({}) {
    composer_ = std::make_unique<ConnectionsComposer>(sessions_, remembered_, discoveredIds_,
                                                      bindings_, staleIds_);
    // Bridge the composer's Observable to a Qt signal so existing QObject
    // consumers can connect. The authoritative value stays connections().value().
    composerSub_ = composer_->state().subscribe(
        [this](const std::vector<ConnectionRow>&) { emit connectionsChanged(); },
        /*emitCurrent=*/false);

    // Feed the upstreams from the manager's Qt signals. poolChanged fires on any
    // per-connection state change (the manager forwards each WifiConnection's
    // `changed`), so the sessions snapshot is rebuilt there; discoveredChanged
    // drives discovery + remembered re-points.
    QObject::connect(wifi_, &net::WifiConnectionManager::poolChanged, this, [this] {
        refreshSessions();
        refreshRemembered();
    });
    QObject::connect(wifi_, &net::WifiConnectionManager::discoveredChanged, this, [this] {
        refreshDiscovered();
        refreshRemembered();
    });
    // The 1 s telemetry tick (latency readout) re-derives the sessions snapshot
    // WITHOUT the poolChanged fan-out (hub/AppModel rebuilds): only the derived
    // rows move, and the composer's distinct-until-changed passes just the real
    // display changes through.
    QObject::connect(wifi_, &net::WifiConnectionManager::poolTelemetryChanged, this,
                     [this] { refreshSessions(); });
    // ConnectionHub owns the binding table; its `changed` fires after a
    // bind/unbind, so mirror the bindings upstream off it.
    QObject::connect(hub_, &net::ConnectionHub::changed, this,
                     &ConnectionCoordinator::refreshBindings);

    // Eager initial population so a subscriber attached right after construction
    // sees the current world (matching SharingStarted.Eagerly).
    refreshSessions();
    refreshRemembered();
    refreshDiscovered();
    refreshBindings();
}

ConnectionCoordinator::~ConnectionCoordinator() = default;

void ConnectionCoordinator::refreshSessions() {
    std::vector<SessionSnapshot> next;
    std::vector<std::string> stale;
    const auto& conns = wifi_->connections();
    next.reserve(static_cast<std::size_t>(conns.size()));
    for (auto it = conns.begin(); it != conns.end(); ++it) {
        const net::WifiConnection* conn = it.value();
        if (conn == nullptr) { continue; }
        const auto& server = conn->server();
        SessionSnapshot s;
        s.id = conn->id().toStdString();
        s.presence = presenceOf(conn->state());
        s.name = server.name.toStdString();
        s.ip = server.ip.toStdString();
        s.udpPort = server.udpPort;
        // Already display-rounded by the connection's alive-poll, so the
        // snapshot's exact == keys distinct-until-changed on visible moves.
        s.latencyOneWayMs = conn->latencyOneWayMs();
        s.latencySamples = conn->latencySamples();
        next.push_back(std::move(s));
        // A parked-Stale session also marks its id stale, so a remembered row at
        // the same id reads "Needs pairing" even after the live row is gone.
        if (conn->state() == net::SessionState::Stale) {
            stale.push_back(conn->id().toStdString());
        }
    }
    std::sort(next.begin(), next.end(),
              [](const SessionSnapshot& a, const SessionSnapshot& b) { return a.id < b.id; });
    std::sort(stale.begin(), stale.end());
    sessions_.set(std::move(next));
    staleIds_.set(std::move(stale));
}

void ConnectionCoordinator::refreshRemembered() {
    std::vector<RememberedSnapshot> next;
    const auto remembered = wifi_->remembered();
    next.reserve(static_cast<std::size_t>(remembered.size()));
    for (const auto& r : remembered) {
        RememberedSnapshot s;
        s.id = r.id.toStdString();
        s.name = r.name.toStdString();
        s.ip = r.ip.toStdString();
        s.udpPort = r.udpPort;
        next.push_back(std::move(s));
    }
    std::sort(next.begin(), next.end(),
              [](const RememberedSnapshot& a, const RememberedSnapshot& b) { return a.id < b.id; });
    remembered_.set(std::move(next));
}

void ConnectionCoordinator::refreshDiscovered() {
    std::vector<std::string> next;
    const auto discovered = wifi_->discoveredServers();
    next.reserve(static_cast<std::size_t>(discovered.size()));
    for (const auto& s : discovered) { next.push_back(s.id().toStdString()); }
    std::sort(next.begin(), next.end());
    next.erase(std::unique(next.begin(), next.end()), next.end());
    discoveredIds_.set(std::move(next));
}

void ConnectionCoordinator::refreshBindings() {
    std::vector<Binding> next;
    const auto bindings = hub_->bindings();
    next.reserve(static_cast<std::size_t>(bindings.size()));
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        next.push_back({it.key().toStdString(), it.value().toStdString()});
    }
    std::sort(next.begin(), next.end(),
              [](const Binding& a, const Binding& b) { return a.slotId < b.slotId; });
    bindings_.set(std::move(next));
}

std::optional<ConnectionRow> ConnectionCoordinator::summary(const std::string& id) const {
    const auto rows = composer_->state().value();
    for (const auto& r : rows) {
        if (r.id == id) { return r; }
    }
    return std::nullopt;
}

void ConnectionCoordinator::bind(const QString& slotId, const QString& connectionId) {
    // ConnectionHub owns the binding table + the descriptor-carrying attachSlot
    // (it resolves the bound pad's type/caps via the capability fns AppModel
    // installed). It emits `changed`, which mirrors the bindings upstream.
    hub_->bind(slotId, connectionId);
}

void ConnectionCoordinator::unbind(const QString& slotId) { hub_->unbind(slotId); }

void ConnectionCoordinator::forgetConnection(const QString& connectionId) {
    // Unbind any slot pointing at this connection first (drops the binding +
    // detaches the satellite slot), then forget the host (row + key + pin).
    // Mirrors android forgetConnection's unbind-then-forget ordering.
    const auto bindings = hub_->bindings();
    for (auto it = bindings.cbegin(); it != bindings.cend(); ++it) {
        if (it.value() == connectionId) { hub_->unbind(it.key()); }
    }
    wifi_->forget(connectionId);
    // forget() only fires poolChanged when there was a LIVE connection to drop;
    // a remembered-but-offline satellite leaves no pooled row, so the remembered
    // upstream wouldn't otherwise re-read. Refresh both here so the forgotten
    // row leaves the derived list immediately. (The composer recomputes once on
    // the resulting upstream set; distinct-until-changed coalesces the pair.)
    refreshRemembered();
    refreshSessions();
}

void ConnectionCoordinator::autoReconnectAll() { wifi_->autoReconnectAll(); }

void ConnectionCoordinator::reconnectConnection(const QString& connectionId) {
    // Prefer the freshest discovered endpoint if this satellite is in the
    // current scan (same machineId id) — its IP is guaranteed current.
    for (const auto& s : wifi_->discoveredServers()) {
        if (s.id() == connectionId) {
            wifi_->connectTo(s);
            return;
        }
    }
    // Not in the current scan: kick a discovery pass so a moved box is relearned
    // (the manager persists any new IP and re-attempts on scan completion), AND
    // attempt the last-known persisted endpoint right now. The key persists (no
    // PIN); if the box is still at its last address this connects immediately,
    // and if it moved the scan-driven relearn picks it up — either way the user
    // no longer has to manually rescan first. Mirrors ConnectionsDialog::onReconnectClicked.
    wifi_->startDiscovery();
    for (const auto& r : wifi_->remembered()) {
        if (r.id == connectionId) {
            wifi_->connectTo(r.toDiscovered());
            return;
        }
    }
}

void ConnectionCoordinator::disconnectConnection(const QString& connectionId) {
    // Graceful teardown only — the remembered row + key stay. The pooled row
    // flips to its post-disconnect presence on the resulting poolChanged.
    wifi_->disconnect(connectionId);
}

} // namespace dish::composer
