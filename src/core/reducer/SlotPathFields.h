// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotPathFields — the PURE, Qt-free mapping from a controller slot's identity
// (vid/pid + whether it is a USB-direct synthetic) crossed against the live USB
// path FSM state (the controllers() map) to the per-slot path UI fields the
// QML Controllers page renders: the current phase, the resolved/desired path,
// whether the device is even path-switchable (a raw-HID-claimable controller),
// and the last Direct-claim failure reason (if any).
//
// ── Why this is a separate pure reducer ───────────────────────────────────────
// AppModel::rebuild() already cross-references the controllers() map to derive
// the synthetic slot list and the twin-dedup suppression. Stamping the path
// fields onto each slot is the same shape of cross-reference, but it is pure
// (slot identity x controllers map -> four values) and therefore testable
// directly without standing up a live UsbGamepadManager (which opens real
// USB/SDL and would hang a unit test). rebuild() resolves each slot's (vid, pid)
// from its source — the bridge device list for an SDL slot, the vpKey string for
// a synthetic — and hands that here; the lookup + the supported/phase/desired
// derivation lives in this one checkable place.
//
// ── The matching rule ─────────────────────────────────────────────────────────
//   * A slot resolves to a UsbController by (vid, pid). If NO controller exists
//     for the model — an Xbox/XInput pad the raw-HID gateway never enumerates,
//     or a slot whose vid/pid SDL could not report (0/0) — `pathSupported` is
//     false and the remaining fields take their inert defaults (Routed /
//     Standard / no failure). The QML hides the path control entirely in that
//     case, so an Xbox pad shows no Standard/Direct toggle.
//   * When a controller IS found, `pathPhase` is its FSM phase, `desiredPath`
//     its resolved desired PathChoice, and `directFailure` its last failure
//     reason (nullopt -> ""). These drive the toggle's reflected state, the
//     in-flight spinner (phase == Claiming), and the inline failure note.

#pragma once

#include "core/reducer/PathChoice.h"
#include "core/reducer/UsbPathMachine.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace dish::reducer {

// The path-related fields stamped onto one ControllerSlot. Pure value type so a
// test can assert the mapping without any Qt / model type.
struct SlotPathFields {
    UsbPhase phase = UsbPhase::Routed;
    PathChoice desired = PathChoice::Standard;
    // True iff a UsbController exists for this model — i.e. the raw-HID gateway
    // enumerates it and a Standard/Direct switch is meaningful. False for an
    // Xbox/XInput pad (no controller) or an identity-less slot.
    bool supported = false;
    // Why the last Direct claim failed for this model; nullopt when none.
    std::optional<DirectClaimFailure> failure;

    bool operator==(const SlotPathFields& o) const {
        return phase == o.phase && desired == o.desired && supported == o.supported &&
               failure == o.failure;
    }
    bool operator!=(const SlotPathFields& o) const { return !(*this == o); }
};

// Pack a (vid, pid) into the controllers()-map key the coordinator uses
// (vendorId<<16 | productId). Identical to UsbGamepadManager::vpKey.
inline int slotPathVpKey(int vendorId, int productId) {
    return (vendorId << 16) | (productId & 0xFFFF);
}

// Resolve a slot's path fields from its identity and the live controllers map.
// `vendorId`/`productId` is the model identity (0/0 means "unknown" — never
// matches). A vid/pid of 0 short-circuits to unsupported so an identity-less SDL
// slot never spuriously pairs with the 0/0 key.
inline SlotPathFields slotPathFields(int vendorId, int productId,
                                     const std::map<int, UsbController>& controllers) {
    SlotPathFields out;
    if (vendorId == 0 || productId == 0) { return out; }
    const auto it = controllers.find(slotPathVpKey(vendorId, productId));
    if (it == controllers.end()) { return out; }
    const UsbController& c = it->second;
    out.supported = true;
    out.phase = c.phase;
    out.desired = c.desired;
    out.failure = c.failure;
    return out;
}

// Whether a slot is mid path-switch — the single derived "loading" state the
// toggle binds to (spinner + disabled). Pure, so the full transition flow is
// testable. True from a pick until the slot reaches a STABLE state matching the
// desired path:
//   * a transitional FSM phase (Claiming = the raw-HID claim; AwaitingFramework =
//     release + settle), OR
//   * the desired path's device form is not present yet — Direct wants a synthetic
//     whose poll rate has been measured (the claim is near-instant but the
//     URB-completion-rate sampler needs a brief window: this is the observed "Hz
//     comes in a moment later"); Standard wants the synthetic gone.
// A terminal failure / RestoreStuck / NeedsReplug is NOT switching — those surface
// an error note, never a perpetual spinner.
inline bool slotPathSwitching(UsbPhase phase, PathChoice desired, bool usbDirect,
                              int directPollHz, bool hasFailure) {
    if (phase == UsbPhase::RestoreStuck || phase == UsbPhase::NeedsReplug) { return false; }
    if (hasFailure) { return false; }
    if (phase == UsbPhase::Claiming || phase == UsbPhase::AwaitingFramework) { return true; }
    if (desired == PathChoice::Direct) { return !usbDirect || directPollHz <= 0; }
    return usbDirect; // desired Standard: still switching while on the synthetic.
}

// Parse a synthetic slot id (the decimal vpKey string AppModel builds the
// synthetic slot's id from) back into its (vid, pid). Returns nullopt if the
// string is not a plain non-negative decimal integer (an SDL slot id like
// "sdl:3" never parses here). Mirrors the vpKey packing: vendorId is the high 16
// bits, productId the low 16.
inline std::optional<std::pair<int, int>> parseSyntheticSlotId(const std::string& id) {
    if (id.empty()) { return std::nullopt; }
    for (char ch : id) {
        if (ch < '0' || ch > '9') { return std::nullopt; }
    }
    // std::stoll over a long all-digit string would throw; the keys we build are
    // a 32-bit packed int, so bound the width before converting.
    if (id.size() > 10) { return std::nullopt; }
    const long long key = std::stoll(id);
    if (key < 0 || key > 0xFFFFFFFFLL) { return std::nullopt; }
    const int vendorId = static_cast<int>((key >> 16) & 0xFFFF);
    const int productId = static_cast<int>(key & 0xFFFF);
    return std::make_pair(vendorId, productId);
}

} // namespace dish::reducer
