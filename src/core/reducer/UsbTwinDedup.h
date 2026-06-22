// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbTwinDedup — the PURE, Qt-free twin-dedup arbitration: given the USB-direct
// (synthetic) claims and the SDL/framework (routed) devices present right now,
// decide which routed device ids must be SUPPRESSED so a pad visible to BOTH the
// SDL/XInput bridge and raw-HID streams via EXACTLY ONE path. Port of dish-android
// ui/main/SyntheticTwinDedup.kt routedTwinIdsHiddenBySynthetics 1:1.
//
// ── Why this exists (the double-input bug it prevents) ────────────────────────
// A HID pad (e.g. a DualSense) appears to SDL/XInput AND to the raw-HID gateway.
// If USB-direct claims it (FSM phase Direct) while SDL also reads it, the satellite
// gets the SAME input twice. That is a bug. This reducer is the single source of
// truth for "which SDL device is hidden because a USB-direct twin is streaming";
// AppModel pushes the result into the SDL bridge's suppression gate so the hidden
// device's INPUT/MOTION/TOUCHPAD never publish. On claim-failure / detach the
// synthetic disappears, the set recomputes empty for that model, and SDL resumes —
// a clean fallback with no replug.
//
// ── The matching rule (identical to android) ──────────────────────────────────
//   * A synthetic with vendorId == 0 || productId == 0 matches nothing (an
//     unidentified claim never hides a real pad).
//   * Synthetics are counted per (vid, pid). For each model, that many routed
//     twins of the SAME (vid, pid) are hidden — the DISCONNECTING ones first
//     (sortedByDescending isDisconnecting), so a live twin survives over a
//     lingering ghost. This count-based pairing is what makes the two-identical-
//     pads case correct: claiming ONE of two identical DualSenses hides exactly
//     ONE routed twin, not both.
//   * Xbox/XInput pads never appear as synthetics (XInput hides them from raw HID
//     and the gateway's enumerate() excludes them), so an Xbox pad's routed entry
//     is never in `synthetics` and is therefore never hidden — Xbox stays on SDL.

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dish::reducer {

// One USB-direct claim that is actively streaming (FSM phase Direct). Only the
// model identity matters for the dedup. A vid/pid of 0 is treated as
// unidentified and matches nothing.
struct SyntheticTwin {
    int vendorId = 0;
    int productId = 0;
};

// One SDL/framework device the bridge currently has open. `id` is the bridge's
// stable per-device id (the "sdl:<iid>" string) used to suppress it; vid/pid is
// its identity; `disconnecting` flags a device in its grace-period teardown (it
// is preferred for hiding so a live twin keeps streaming).
struct RoutedDevice {
    std::string id;
    int vendorId = 0;
    int productId = 0;
    bool disconnecting = false;
};

// Pack a (vid, pid) into one comparable key for the per-model grouping.
inline std::int64_t twinModelKey(int vendorId, int productId) {
    return (static_cast<std::int64_t>(vendorId) << 32) |
           (static_cast<std::int64_t>(productId) & 0xFFFFFFFF);
}

// Returns the set of routed device ids hidden by an active USB-direct synthetic of
// the same model. 1:1 with android's routedTwinIdsHiddenBySynthetics: count the
// synthetics per model, then for each model hide that many routed twins, the
// disconnecting ones first.
inline std::set<std::string> suppressedRoutedIds(const std::vector<SyntheticTwin>& synthetics,
                                                 const std::vector<RoutedDevice>& routed) {
    std::map<std::int64_t, int> syntheticModelCounts;
    for (const auto& s : synthetics) {
        if (s.vendorId == 0 || s.productId == 0) { continue; }
        ++syntheticModelCounts[twinModelKey(s.vendorId, s.productId)];
    }
    std::set<std::string> hidden;
    if (syntheticModelCounts.empty()) { return hidden; }

    // Group routed devices of a matched model, preserving input order so the
    // stable-sort tie-break matches android's (input-order within equal
    // disconnecting flag).
    std::map<std::int64_t, std::vector<RoutedDevice>> byModel;
    for (const auto& r : routed) {
        const std::int64_t key = twinModelKey(r.vendorId, r.productId);
        if (syntheticModelCounts.find(key) == syntheticModelCounts.end()) { continue; }
        byModel[key].push_back(r);
    }
    for (auto& [key, group] : byModel) {
        // Disconnecting first (stable_sort: equal flags keep input order).
        std::stable_sort(group.begin(), group.end(),
                         [](const RoutedDevice& a, const RoutedDevice& b) {
                             return a.disconnecting && !b.disconnecting;
                         });
        const int take = syntheticModelCounts[key];
        for (int i = 0; i < take && i < static_cast<int>(group.size()); ++i) {
            hidden.insert(group[static_cast<std::size_t>(i)].id);
        }
    }
    return hidden;
}

} // namespace dish::reducer
