// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/ConnectionsComposer.h"

#include <algorithm>
#include <set>
#include <unordered_map>

namespace dish::composer {

namespace {

// The slot id bound to a connection id, or empty. Android collects all bound
// slots; Windows binds one physical pad per connection-slot pair, so the first
// match is the row's bound slot. Deterministic: the bindings vector is built
// sorted by the Coordinator.
std::string boundSlotFor(const std::string& connId, const std::vector<Binding>& bindings) {
    for (const auto& b : bindings) {
        if (b.connectionId == connId) { return b.slotId; }
    }
    return {};
}

} // namespace

std::vector<ConnectionRow> buildConnectionSummaries(
    const std::vector<SessionSnapshot>& sessions, const std::vector<RememberedSnapshot>& remembered,
    const std::vector<std::string>& discoveredIds, const std::vector<Binding>& bindings,
    const std::vector<std::string>& staleIds) {
    const std::set<std::string> discovered(discoveredIds.begin(), discoveredIds.end());
    const std::set<std::string> stale(staleIds.begin(), staleIds.end());

    std::unordered_map<std::string, const SessionSnapshot*> sessionById;
    for (const auto& s : sessions) { sessionById.emplace(s.id, &s); }
    std::unordered_map<std::string, const RememberedSnapshot*> rememberedById;
    for (const auto& r : remembered) { rememberedById.emplace(r.id, &r); }

    // The id universe is (remembered ∪ live) — a live-but-unremembered session
    // (mid-pair, before the first PUT remembers it) still gets a row, and a
    // remembered-but-offline satellite still gets one. Mirrors android
    // (rememberedById.keys + satMap.keys).
    std::set<std::string> ids;
    for (const auto& [id, s] : sessionById) { ids.insert(id); }
    for (const auto& [id, r] : rememberedById) { ids.insert(id); }

    std::vector<ConnectionRow> out;
    out.reserve(ids.size());
    for (const auto& id : ids) {
        const SessionSnapshot* conn = nullptr;
        if (const auto it = sessionById.find(id); it != sessionById.end()) { conn = it->second; }
        const RememberedSnapshot* rem = nullptr;
        if (const auto it = rememberedById.find(id); it != rememberedById.end()) {
            rem = it->second;
        }

        // Prefer the live session's server (its address may be fresher than the
        // remembered row), else the remembered row. If neither names an ip the
        // row is invalid (a ghost) and is dropped — mirrors the ?: return null.
        std::string name;
        std::string ip;
        int udpPort = 0;
        if (conn != nullptr && !conn->ip.empty()) {
            name = conn->name;
            ip = conn->ip;
            udpPort = conn->udpPort;
        } else if (rem != nullptr && !rem->ip.empty()) {
            name = rem->name;
            ip = rem->ip;
            udpPort = rem->udpPort;
        } else {
            continue;
        }

        const reducer::SessionPresence presence =
            (conn != nullptr) ? conn->presence : reducer::SessionPresence::Idle;
        const reducer::UiLinkState live = reducer::satelliteLinkState(
            presence, /*isStale=*/stale.count(id) != 0, /*isDiscovered=*/discovered.count(id) != 0);

        ConnectionRow row;
        row.id = id;
        row.label = name.empty() ? ip : name;
        row.live = live;
        row.kind = reducer::ConnectionKind::Satellite;
        row.detailKey = reducer::RowDetailKey::DiscoveredRow;
        row.ip = ip;
        row.udpPort = udpPort;
        row.boundSlotId = boundSlotFor(id, bindings);
        // The latency readout travels only with a live session's snapshot — a
        // remembered-only row keeps the 0 / 0 default.
        if (conn != nullptr) {
            row.latencyOneWayMs = conn->latencyOneWayMs;
            row.latencySamples = conn->latencySamples;
        }
        row.glyph = reducer::glyphForConnection(row.kind, live);
        row.dotColor = reducer::dotColorForState(live);
        row.chip = reducer::statusChipKey(live);
        out.push_back(std::move(row));
    }

    // Stable order so distinct-until-changed never re-emits on map iteration
    // order alone. Sort by label, then id as a tiebreaker (two boxes can share
    // a label).
    std::sort(out.begin(), out.end(), [](const ConnectionRow& a, const ConnectionRow& b) {
        if (a.label != b.label) { return a.label < b.label; }
        return a.id < b.id;
    });
    return out;
}

} // namespace dish::composer
