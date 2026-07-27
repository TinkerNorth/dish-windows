// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// ConfigBlocker — the binding-config screen's blocker rule, pure and Qt-free.
// Port of dish-android ui/main/ConfigureBindingsViewModel.kt's
// ConfigUiState.blocker (the rule ConfigUiStateBlockerTest pins, 13 cases),
// closing the "no pure home in Windows src" gap flagged in tests/PARITY.md.
//
// The rule decides which full-screen interruption (if any) the configure flow
// shows, in strict precedence:
//   * nothing until the initial state load completes (no flash-of-blocker);
//   * the controller vanishing blocks EVERYTHING (there is nothing to bind);
//   * no host selected -> no host blocker (the picker itself is the UI);
//   * a host that is not live (unknown, or any non-Connected/Unstable link)
//     -> HostLost, labelled from the summary, else the last-known label, else
//     empty; `reconnecting` is true only while a reconnect is in flight;
//   * an Unstable host -> HostUnsteady, DISMISSIBLE per host id — but a
//     dismissal never suppresses a real loss, and a dismissal for another host
//     hides nothing.
//
// Emits kind tokens + the raw host label, never localized text — the view maps
// tokens to strings. LinkState vocabulary comes from SatelliteLinkState.h
// (already the android LinkState mirror).

#pragma once

#include "core/reducer/SatelliteLinkState.h"

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace dish::reducer {

// android ControllerAdapter.isLiveLink: the two states that actually stream.
inline bool isLiveLink(UiLinkState s) {
    return s == UiLinkState::Connected || s == UiLinkState::Unstable;
}

// The blocker verdict. Mirrors android's sealed BindingBlocker (+None for the
// nullable): HostLost carries the label to render and whether a reconnect is
// already in flight (the view swaps "reconnect" for a spinner then).
enum class ConfigBlockerKind { None, InputLost, HostLost, HostUnsteady };

struct ConfigBlocker {
    ConfigBlockerKind kind = ConfigBlockerKind::None;
    std::string hostLabel;     // HostLost only
    bool reconnecting = false; // HostLost only

    bool operator==(const ConfigBlocker& o) const {
        return kind == o.kind && hostLabel == o.hostLabel && reconnecting == o.reconnecting;
    }
    bool operator!=(const ConfigBlocker& o) const { return !(*this == o); }
};

// The ConnectionSummary subset the rule reads — id, display label, link state.
// Plain values so the caller can project from whatever summary type it holds.
struct BlockerHostRow {
    std::string id;
    std::string label;
    UiLinkState live = UiLinkState::Saved;
};

// The pure rule. `hostId` empty models "no host selected" (android's null
// draft.hostId); `knownHostLabels` is the remembered id->label map a forgotten
// host's blocker falls back to; `dismissedUnsteadyHostIds` are per-host
// unsteady-warning dismissals.
inline ConfigBlocker
configBlockerFor(bool loaded, bool controllerPresent, const std::optional<std::string>& hostId,
                 const std::vector<BlockerHostRow>& connections,
                 const std::map<std::string, std::string>& knownHostLabels,
                 const std::set<std::string>& dismissedUnsteadyHostIds) {
    if (!loaded) { return {}; }
    if (!controllerPresent) { return ConfigBlocker{ConfigBlockerKind::InputLost, {}, false}; }
    if (!hostId.has_value()) { return {}; }

    const BlockerHostRow* summary = nullptr;
    for (const auto& row : connections) {
        if (row.id == *hostId) {
            summary = &row;
            break;
        }
    }

    if (summary == nullptr || !isLiveLink(summary->live)) {
        ConfigBlocker lost;
        lost.kind = ConfigBlockerKind::HostLost;
        if (summary != nullptr) {
            lost.hostLabel = summary->label;
        } else {
            const auto it = knownHostLabels.find(*hostId);
            if (it != knownHostLabels.end()) { lost.hostLabel = it->second; }
        }
        lost.reconnecting = summary != nullptr && summary->live == UiLinkState::Connecting;
        return lost;
    }
    if (summary->live == UiLinkState::Unstable &&
        dismissedUnsteadyHostIds.find(*hostId) == dismissedUnsteadyHostIds.end()) {
        return ConfigBlocker{ConfigBlockerKind::HostUnsteady, {}, false};
    }
    return {};
}

} // namespace dish::reducer
