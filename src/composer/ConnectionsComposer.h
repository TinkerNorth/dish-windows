// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ConnectionsComposer — a kernel Composer that PURELY derives the flat
// connections list the UI consumes by combining the upstream Observables
// (per-session presence, discovered ids, remembered satellites, slot bindings,
// stale ids) through one pure transform. It replaces the imperative aggregation
// that lived in ConnectionHub::rebuild. Per the SoC rules a Composer never
// touches Qt/tr()/widget types: the transform is a free function over Qt-free
// snapshot value types, the per-row LinkState comes from the pure
// reducer/satelliteLinkState mapper, and the row label/detail are carried as
// data (a key + args) for the UI to localize. Mirrors dish-android
// composer/ConnectionsComposer (buildSummaries), with its Context.getString
// localization leak pushed up to the UI.

#pragma once

#include "architecture/Composer.h"
#include "architecture/Observable.h"
#include "core/reducer/ConnectionRows.h"
#include "core/reducer/SatelliteLinkState.h"

#include <cstdint>
#include <string>
#include <vector>

namespace dish::composer {

// ── Upstream snapshot value types (Qt-free, copyable, == comparable) ─────────

// One live-or-potential session as the composer sees it: its stable id, the
// presence axis, and the bits of the server needed to render a row. Fed from
// the manager's per-connection state (WifiConnection) at the Qt boundary.
struct SessionSnapshot {
    std::string id;
    reducer::SessionPresence presence = reducer::SessionPresence::Idle;
    std::string name; // server display name (may be empty)
    std::string ip;
    int udpPort = 0;
    // One-way latency readout for a live session (median heartbeat-RTT/2, ms,
    // already display-rounded by the WifiConnection poll) + the RTT sample
    // count. 0 / 0 while idle or unseeded. Exact == is safe: both sides carry
    // the same rounded value, so distinct-until-changed keys on display moves.
    double latencyOneWayMs = 0.0;
    int latencySamples = 0;

    bool operator==(const SessionSnapshot& o) const {
        return id == o.id && presence == o.presence && name == o.name && ip == o.ip &&
               udpPort == o.udpPort && latencyOneWayMs == o.latencyOneWayMs &&
               latencySamples == o.latencySamples;
    }
    bool operator!=(const SessionSnapshot& o) const { return !(*this == o); }
};

// A remembered (persisted) satellite, reduced to what a row needs. A remembered
// entry with no live session still renders (Offline / Stale / Ready).
struct RememberedSnapshot {
    std::string id;
    std::string name;
    std::string ip;
    int udpPort = 0;

    bool operator==(const RememberedSnapshot& o) const {
        return id == o.id && name == o.name && ip == o.ip && udpPort == o.udpPort;
    }
    bool operator!=(const RememberedSnapshot& o) const { return !(*this == o); }
};

// ── The derived row (the composer's output element) ──────────────────────────

// One connections-list row. Qt-free + == comparable so the Observable's
// distinct-until-changed suppresses no-op re-emits. `detailKey` + (ip, udpPort)
// replace android's pre-localized `detail` string — the UI formats it. `glyph`
// / `dotColor` / `chip` are the pure-mapped render keys.
struct ConnectionRow {
    std::string id;
    std::string label; // server name, or ip when the name is empty
    reducer::UiLinkState live = reducer::UiLinkState::Saved;
    reducer::ConnectionKind kind = reducer::ConnectionKind::Satellite;
    // Detail line as data, not localized text (UI formats "<ip> • UDP <port>").
    reducer::RowDetailKey detailKey = reducer::RowDetailKey::DiscoveredRow;
    std::string ip;
    int udpPort = 0;
    // The slot id bound to this connection, if any (empty = unbound). Android
    // carries a list; Windows binds one physical pad per connection-slot pair,
    // so a single bound slot id is enough for the row.
    std::string boundSlotId;
    // Pre-mapped render keys (so the UI never re-derives them and can't drift).
    reducer::ConnectionGlyph glyph = reducer::ConnectionGlyph::SatelliteBase;
    reducer::DotColor dotColor = reducer::DotColor::Muted;
    reducer::StatusChipKey chip = reducer::StatusChipKey::Offline;
    // One-way latency readout carried from the live session's snapshot (0 / 0
    // for a remembered-only row). The UI gates the caption on a Connected row
    // with latencySamples > 0 and shows the count beside the figure.
    double latencyOneWayMs = 0.0;
    int latencySamples = 0;

    bool operator==(const ConnectionRow& o) const {
        return id == o.id && label == o.label && live == o.live && kind == o.kind &&
               detailKey == o.detailKey && ip == o.ip && udpPort == o.udpPort &&
               boundSlotId == o.boundSlotId && glyph == o.glyph && dotColor == o.dotColor &&
               chip == o.chip && latencyOneWayMs == o.latencyOneWayMs &&
               latencySamples == o.latencySamples;
    }
    bool operator!=(const ConnectionRow& o) const { return !(*this == o); }
};

// One slotId -> connectionId binding. A plain pair so the bindings upstream is a
// Qt-free, copyable, == comparable vector.
struct Binding {
    std::string slotId;
    std::string connectionId;

    bool operator==(const Binding& o) const {
        return slotId == o.slotId && connectionId == o.connectionId;
    }
};

// ── The pure transform (the heart — unit-tested directly + via ComposerProbe) ─

// Derive the connections list from the upstream snapshots. Pure: no Qt, no IO,
// no events. For each id in (remembered ∪ live), pick the live session's server
// if present else the remembered row, derive the LinkState via
// satelliteLinkState (presence + isStale + isDiscovered), attach the bound slot,
// and map the render keys. Sorted by label so the list is stable for
// distinct-until-changed. Mirrors android buildSummaries/buildSatelliteSummary.
std::vector<ConnectionRow> buildConnectionSummaries(
    const std::vector<SessionSnapshot>& sessions, const std::vector<RememberedSnapshot>& remembered,
    const std::vector<std::string>& discoveredIds, const std::vector<Binding>& bindings,
    const std::vector<std::string>& staleIds);

// ── The Composer (combines the 5 upstreams via the pure transform) ───────────

// The five upstream Observables the composer combines. Owned by the
// Coordinator (which feeds them from the Qt signal world); the composer only
// reads them. The transform is buildConnectionSummaries above.
class ConnectionsComposer
    : public arch::Composer<std::vector<ConnectionRow>, std::vector<SessionSnapshot>,
                            std::vector<RememberedSnapshot>, std::vector<std::string>,
                            std::vector<Binding>, std::vector<std::string>> {
  public:
    ConnectionsComposer(const arch::Observable<std::vector<SessionSnapshot>>& sessions,
                        const arch::Observable<std::vector<RememberedSnapshot>>& remembered,
                        const arch::Observable<std::vector<std::string>>& discoveredIds,
                        const arch::Observable<std::vector<Binding>>& bindings,
                        const arch::Observable<std::vector<std::string>>& staleIds)
        : arch::Composer<std::vector<ConnectionRow>, std::vector<SessionSnapshot>,
                         std::vector<RememberedSnapshot>, std::vector<std::string>,
                         std::vector<Binding>, std::vector<std::string>>(
              sessions, remembered, discoveredIds, bindings, staleIds,
              [](const std::vector<SessionSnapshot>& s, const std::vector<RememberedSnapshot>& r,
                 const std::vector<std::string>& d, const std::vector<Binding>& b,
                 const std::vector<std::string>& st) {
                  return buildConnectionSummaries(s, r, d, b, st);
              }) {}
};

} // namespace dish::composer
