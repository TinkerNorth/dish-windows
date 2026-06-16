// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// QML migration — the bind-chooser availability exposure
// (AppViewModel::availableConnectionsForSlot). The view model itself can't be
// instantiated in this harness (it owns a full AppModel: SDL bridge, theme
// controller over qApp, processor — and its live network methods open real
// sockets the process would join on exit), so this pins the REDUCER-SEAM
// contract the invokable is assembled from, the same way
// test_app_view_model_connections pins its data contract.
//
// The invokable does exactly two pure things over MainUiState
// (st.connections + the slot's boundConnectionId):
//   1. candidate filter — drop connections bound to ANOTHER slot (mirrors
//      MainWindow.cpp:228-232's `available`), but KEEP the slot's own binding;
//   2. reducer::connectionsVisibleInPicker(candidates, boundConnectionId) — keep
//      the live-available ones + hold over the slot's own binding even offline.
// We replicate that two-stage derivation here (a hand-built ConnectionSummary
// list + bindings) and assert: a connection bound to ANOTHER slot is EXCLUDED,
// an unbound-available one is INCLUDED, and the slot's own held-over binding is
// INCLUDED even when offline.

#include "Models/Models.h"
#include "core/reducer/PickerVisibility.h"

#include <catch2/catch_test_macros.hpp>

#include <QList>
#include <QString>

#include <optional>

using dish::models::ConnectionSummary;
using dish::models::LinkState;
using dish::reducer::connectionsVisibleInPicker;

namespace {

ConnectionSummary conn(const QString& id, LinkState live,
                       std::optional<QString> boundSlotId = std::nullopt) {
    ConnectionSummary c;
    c.id = id;
    c.label = id;
    c.live = live;
    c.boundSlotId = std::move(boundSlotId);
    return c;
}

QList<QString> ids(const QList<ConnectionSummary>& list) {
    QList<QString> out;
    for (const auto& c : list) { out.push_back(c.id); }
    return out;
}

// The exact derivation AppViewModel::availableConnectionsForSlot performs over
// MainUiState (before the {connectionId,label,dotColor,glyph} JS projection):
// candidate filter (not bound elsewhere) then the picker-visibility reducer with
// the slot's own boundConnectionId as the holdover.
QList<ConnectionSummary> availableForSlot(const QList<ConnectionSummary>& connections,
                                          const QString& slotId,
                                          const std::optional<QString>& boundConnectionId) {
    QList<ConnectionSummary> candidates;
    for (const auto& c : connections) {
        const bool boundElsewhere = c.boundSlotId.has_value() && *c.boundSlotId != slotId;
        if (!boundElsewhere) { candidates.append(c); }
    }
    return connectionsVisibleInPicker(candidates, boundConnectionId);
}

} // namespace

TEST_CASE("bind availability: a connection bound to ANOTHER slot is excluded",
          "[appvm][bind]") {
    // s:other is online but already routed to slot-B; the picker for slot-A must
    // not offer it (the Widgets `available` filter the QML page was missing).
    const QList<ConnectionSummary> connections{
        conn("s:free", LinkState::Connected),
        conn("s:other", LinkState::Connected, QStringLiteral("slot-B"))};

    const auto visible = availableForSlot(connections, QStringLiteral("slot-A"), std::nullopt);
    REQUIRE(ids(visible) == QList<QString>{"s:free"});
}

TEST_CASE("bind availability: an unbound live connection is included", "[appvm][bind]") {
    const QList<ConnectionSummary> connections{conn("s:free", LinkState::Connected)};
    const auto visible = availableForSlot(connections, QStringLiteral("slot-A"), std::nullopt);
    REQUIRE(ids(visible) == QList<QString>{"s:free"});
}

TEST_CASE("bind availability: an unbound OFFLINE connection is hidden", "[appvm][bind]") {
    // Not bound anywhere but offline -> not a usable pick (Saved is not available).
    const QList<ConnectionSummary> connections{conn("s:offline", LinkState::Saved)};
    const auto visible = availableForSlot(connections, QStringLiteral("slot-A"), std::nullopt);
    REQUIRE(visible.isEmpty());
}

TEST_CASE("bind availability: the slot's OWN binding is held over even when offline",
          "[appvm][bind]") {
    // slot-A is bound to s:mine, which has gone offline (Saved). It still carries
    // boundSlotId == slot-A, so the candidate filter keeps it (own slot, not
    // elsewhere) and the reducer holds it over via the boundConnectionId arg.
    const QList<ConnectionSummary> connections{
        conn("s:mine", LinkState::Saved, QStringLiteral("slot-A")),
        conn("s:free", LinkState::Connected)};

    const auto visible = availableForSlot(connections, QStringLiteral("slot-A"),
                                          std::optional<QString>("s:mine"));
    // Both: the live unbound one AND the slot's offline holdover.
    REQUIRE(ids(visible) == QList<QString>({"s:mine", "s:free"}));
}

TEST_CASE("bind availability: excluded-elsewhere + included-unbound + held-over together",
          "[appvm][bind]") {
    // The full scenario the task names, in one list: another slot's binding is
    // dropped, an unbound-available one is offered, and slot-A's own offline
    // binding survives.
    const QList<ConnectionSummary> connections{
        conn("s:mine", LinkState::Saved, QStringLiteral("slot-A")),     // own holdover (offline)
        conn("s:elsewhere", LinkState::Connected, QStringLiteral("slot-B")), // bound elsewhere
        conn("s:free", LinkState::Connected),                          // unbound + available
        conn("s:offline", LinkState::Saved)};                          // unbound + offline

    const auto visible = availableForSlot(connections, QStringLiteral("slot-A"),
                                          std::optional<QString>("s:mine"));
    REQUIRE(ids(visible) == QList<QString>({"s:mine", "s:free"}));
}
