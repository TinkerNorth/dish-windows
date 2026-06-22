// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Descriptor-aware late-slot converge (contract §Session). A session PUT
// snapshots the desired descriptors at send time; slots can change between the
// snapshot and the response landing. This diff returns the per-controller
// follow-ups to converge the live session WITHOUT re-PUTting the whole thing.
//
// This is a STRICTER companion to reducer/lateSlotConverge in Reconcile.h (which
// diffs only on (ctrlIdx, type)). dish-android's lateSlotConverge keys the diff
// on the WHOLE descriptor — a touchpad-mode flip or a caps change (e.g.
// CAP_MOTION gained) on the same index is a resync too, not just a type change.
// Wave 1 placed the (ctrlIdx,type) variant in core/reducer; this slice (2b)
// lands the descriptor-aware variant the android LateSlotConvergeTest pins.
// Pure, Qt-free, socket-free.

#pragma once

#include "core/model/Protocol.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <vector>

namespace dish::reducer {

// One desired controller descriptor reduced to the fields a descriptor change
// is keyed on: index + emulation type + caps word + touchpad routing mode.
// These are exactly the fields the per-controller PUT body carries, so any one
// of them differing means the server's applied descriptor is stale.
struct DescriptorSlot {
    std::uint8_t ctrlIdx = 0;
    std::uint8_t type = proto::kControllerTypeXbox;
    std::uint16_t caps = 0;
    std::uint8_t touchpadMode = proto::kTouchpadModeOff;

    // Field-wise equality EXCLUDING ctrlIdx — the index is the map key, the
    // rest is "did the descriptor at this index change?".
    bool sameDescriptorAs(const DescriptorSlot& o) const {
        return type == o.type && caps == o.caps && touchpadMode == o.touchpadMode;
    }
};

// The per-controller follow-ups: `resyncs` are ctrlIdx whose descriptor changed
// (or is newly desired) and must be re-PUT via PUT /controllers/{idx};
// `deletes` are ctrlIdx that were sent but no longer desired and must be
// DELETEd. Both ascending so the order is deterministic for tests + replay.
struct LateConvergeDesc {
    std::vector<std::uint8_t> resyncs;
    std::vector<std::uint8_t> deletes;

    bool operator==(const LateConvergeDesc& o) const {
        return resyncs == o.resyncs && deletes == o.deletes;
    }
};

// Diff the snapshot `sent` against the current `desired`. An index present in
// `desired` but absent from `sent`, or present in both with ANY field of the
// descriptor different, is a resync; an index in `sent` but not `desired` is a
// delete. An index re-used with an identical descriptor is left alone. Mirrors
// android lateSlotConverge.
inline LateConvergeDesc lateSlotConvergeDesc(const std::vector<DescriptorSlot>& sent,
                                             const std::vector<DescriptorSlot>& desired) {
    LateConvergeDesc out;
    std::map<std::uint8_t, DescriptorSlot> sentByIdx;
    for (const auto& s : sent) { sentByIdx[s.ctrlIdx] = s; }
    std::map<std::uint8_t, DescriptorSlot> desiredByIdx;
    for (const auto& d : desired) { desiredByIdx[d.ctrlIdx] = d; }

    for (const auto& [idx, d] : desiredByIdx) {
        const auto it = sentByIdx.find(idx);
        if (it == sentByIdx.end() || !it->second.sameDescriptorAs(d)) {
            out.resyncs.push_back(idx);
        }
    }
    for (const auto& [idx, s] : sentByIdx) {
        if (desiredByIdx.find(idx) == desiredByIdx.end()) { out.deletes.push_back(idx); }
    }
    std::sort(out.resyncs.begin(), out.resyncs.end());
    std::sort(out.deletes.begin(), out.deletes.end());
    return out;
}

} // namespace dish::reducer
