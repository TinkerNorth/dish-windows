// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure FOUND-list visibility mapper: which discovered satellites the
// Connections page's FOUND card offers. Pulled out of the view model so the
// one-spot rule is unit-testable without standing up the AppModel graph
// (AppViewModel itself can't be instantiated in the test harness). Qt-free in
// spirit (it only reads tiny value views), no IO, no events — same shape as
// PickerVisibility.h.

#pragma once

#include "Models/Models.h"

#include <QList>
#include <QSet>
#include <QString>

namespace dish::reducer {

// The one-spot rule: a satellite renders in EXACTLY ONE of the two Connections
// lists. Any id that already has a row in the derived connections list
// (remembered ∪ live sessions — the REMEMBERED card) renders THERE, where its
// chip (Ready / Online / Needs pairing / …) already carries reachability and
// the row carries the state-appropriate actions; the FOUND card offers only
// the rest — new, un-remembered boxes whose sole action is Pair. The row-id
// set (not just remembered ids) is the key so a live-but-not-yet-remembered
// session (mid-pair, before the first PUT persists it) also collapses to its
// connections row instead of showing twice.
//
// Identity is the stable machineId-preferring key on BOTH sides
// (DiscoveredServer::id() == the remembered row id), so a remembered box
// re-discovered at a fresh DHCP address still folds into its remembered row.
// Order is preserved (the scan order is the FOUND row order) and the input is
// never mutated.
inline QList<models::DiscoveredServer>
serversVisibleInFound(const QList<models::DiscoveredServer>& discovered,
                      const QSet<QString>& connectionRowIds) {
    QList<models::DiscoveredServer> visible;
    visible.reserve(discovered.size());
    for (const auto& s : discovered) {
        if (!connectionRowIds.contains(s.id())) { visible.push_back(s); }
    }
    return visible;
}

} // namespace dish::reducer
