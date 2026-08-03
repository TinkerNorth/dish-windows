// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Twin-dedup arbitration. A HID pad such as a DualSense is visible to both
// SDL/XInput and the raw-HID gateway; if USB-direct claims it while SDL also
// reads it, the satellite receives the same input twice. This decides which
// routed device ids the bridge must suppress so a pad streams over exactly one
// path. When a claim fails or the device detaches the synthetic disappears, the
// set recomputes empty and SDL resumes with no replug.
//
// Pairing is count-based per (vid, pid), which is what makes two identical pads
// correct: claiming one of two DualSenses hides exactly one routed twin. Xbox
// pads never appear as synthetics, since XInput hides them from raw HID, so they
// always stay on SDL.

#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace dish::reducer {

// A USB-direct claim that is actively streaming. A vid or pid of 0 is
// unidentified and matches nothing, so it never hides a real pad.
struct SyntheticTwin {
    int vendorId = 0;
    int productId = 0;
};

// `id` is the bridge's stable "sdl:<iid>" id. `disconnecting` marks a device in
// grace-period teardown; those are hidden first so a live twin keeps streaming.
struct RoutedDevice {
    std::string id;
    int vendorId = 0;
    int productId = 0;
    bool disconnecting = false;
};

inline std::int64_t twinModelKey(int vendorId, int productId) {
    return (static_cast<std::int64_t>(vendorId) << 32) |
           (static_cast<std::int64_t>(productId) & 0xFFFFFFFF);
}

inline std::set<std::string> suppressedRoutedIds(const std::vector<SyntheticTwin>& synthetics,
                                                 const std::vector<RoutedDevice>& routed) {
    std::map<std::int64_t, int> syntheticModelCounts;
    for (const auto& s : synthetics) {
        if (s.vendorId == 0 || s.productId == 0) { continue; }
        ++syntheticModelCounts[twinModelKey(s.vendorId, s.productId)];
    }
    std::set<std::string> hidden;
    if (syntheticModelCounts.empty()) { return hidden; }

    // Input order is preserved so the stable-sort tie-break stays deterministic.
    std::map<std::int64_t, std::vector<RoutedDevice>> byModel;
    for (const auto& r : routed) {
        const std::int64_t key = twinModelKey(r.vendorId, r.productId);
        if (syntheticModelCounts.find(key) == syntheticModelCounts.end()) { continue; }
        byModel[key].push_back(r);
    }
    for (auto& [key, group] : byModel) {
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
