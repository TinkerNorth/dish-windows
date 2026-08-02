// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// BindingPresence — the presence gate over the slot->connection binding table:
// which bindings still have a physical pad behind them.
//
// A binding is not a preference, it is a DECLARATION: ConnectionHub::bind hands
// WifiConnection::attachSlot a descriptor, and every session PUT re-sends the
// whole desired set (WifiConnection::desiredDescriptors). The satellite creates
// a virtual pad for each one. Nothing removed a binding when its physical pad
// went away, so the next reconnect re-plugged a virtual controller backed by no
// hardware at all — the app reported a device that does not exist.
//
// The gate is one pure decision over (slots the app currently shows) x
// (bindings the hub holds):
//
//   * pad present            -> keep. The common case; no action.
//   * pad moved to its twin  -> MIGRATE. A USB-direct claim retires the
//                               framework slot id and publishes a synthetic in
//                               its place (and a release does the reverse). The
//                               pad is still there, so the binding follows it —
//                               tearing it down would kill a working stream on
//                               every path switch.
//   * pad gone               -> UNBIND. ConnectionHub::unbind detaches the slot,
//                               which DELETEs the controller on a live session,
//                               so the phantom disappears from the satellite
//                               rather than waiting for a reaper timeout.
//
// Identity is the pad's USB (vid, pid). 0:0 is identity-LESS (the descriptor was
// unreadable), never a key — it matches nothing, mirroring EmulateSeed's
// vidPidKey rule. Pure, Qt-free, socket-free.

#pragma once

#include <algorithm>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dish::reducer {

// One slot the app currently shows (an AppModel::rebuild output row), with the
// physical pad identity behind it.
struct PresentSlot {
    std::string id;
    int vendorId = 0;
    int productId = 0;
};

// One row of the hub's binding table, plus the identity of the pad the binding
// was made against when the app can still resolve it (a framework twin hidden
// by an active claim is still enumerated; a synthetic slot id parses back to
// its vid/pid). nullopt = nothing to match on, so the pad counts as gone.
struct BoundSlot {
    std::string slotId;
    std::string connId;
    std::optional<std::pair<int, int>> identity;
};

enum class BindingPresenceKind { Unbind, Migrate };

// `slotId` is always the binding to drop, and `connId` always the connection it
// pointed at — on a Migrate that is the connection to re-bind `toSlotId` to, and
// on an Unbind it is who to NAME in the notice (a binding that vanishes with no
// explanation reads as a bug to the person watching the slot card). `toSlotId`
// is meaningful for Migrate only.
struct BindingPresenceAction {
    BindingPresenceKind kind = BindingPresenceKind::Unbind;
    std::string slotId;
    std::string toSlotId;
    std::string connId;
};

// A real pad identity — 0:0 is the identity-less sentinel, not a key.
inline bool isPadIdentity(const std::optional<std::pair<int, int>>& identity) {
    return identity.has_value() && !(identity->first == 0 && identity->second == 0);
}

// The pad identity behind `slotId`, or nullopt when the slot is not shown (or
// carries no readable identity). The seam the emulation-type seed reads: the
// catalog's `emulates` hints are matched against the pad's real identity, so a
// call site that cannot answer this question silently degrades every pad to the
// catalog's first offered type.
inline std::optional<std::pair<int, int>> padIdentityFor(const std::string& slotId,
                                                         const std::vector<PresentSlot>& present) {
    for (const auto& s : present) {
        if (s.id != slotId) { continue; }
        const auto identity = std::make_optional(std::make_pair(s.vendorId, s.productId));
        return isPadIdentity(identity) ? identity : std::nullopt;
    }
    return std::nullopt;
}

// The follow-ups that reconcile the binding table with what is actually
// plugged in. Ordered by the departing slot id so the result is deterministic
// for tests + replay. Idempotent: re-running over the shape its own actions
// produce yields nothing (AppModel re-enters rebuild after applying them).
inline std::vector<BindingPresenceAction>
resolveBindingPresence(const std::vector<PresentSlot>& present,
                       const std::vector<BoundSlot>& bindings) {
    std::map<std::string, const PresentSlot*> presentById;
    for (const auto& s : present) { presentById.emplace(s.id, &s); }

    // Bound slot ids, so a migration never steals a target that already carries
    // a binding of its own (the same model plugged twice).
    std::map<std::string, bool> boundIds;
    for (const auto& b : bindings) { boundIds.emplace(b.slotId, true); }

    // std::map keys the walk by slot id — deterministic order, no post-sort.
    std::map<std::string, const BoundSlot*> bySlotId;
    for (const auto& b : bindings) { bySlotId.emplace(b.slotId, &b); }

    std::vector<BindingPresenceAction> out;
    for (const auto& [slotId, binding] : bySlotId) {
        if (presentById.count(slotId) != 0) { continue; } // the pad is right there

        BindingPresenceAction action;
        action.slotId = slotId;
        action.connId = binding->connId;
        action.kind = BindingPresenceKind::Unbind;

        if (isPadIdentity(binding->identity)) {
            // Same pad, different slot id: the twin transition. First match in
            // the caller's (deterministic) slot order wins.
            for (const auto& candidate : present) {
                if (candidate.vendorId != binding->identity->first ||
                    candidate.productId != binding->identity->second) {
                    continue;
                }
                if (boundIds.count(candidate.id) != 0) { continue; }
                action.kind = BindingPresenceKind::Migrate;
                action.toSlotId = candidate.id;
                break;
            }
        }
        out.push_back(std::move(action));
    }
    return out;
}

} // namespace dish::reducer
