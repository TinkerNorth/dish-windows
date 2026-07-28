// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure touchpad-mode resolution for the descriptor's `touchpadMode` field —
// the ds4 > mouse > off ladder that turns the user's per-satellite pick into
// the EFFECTIVE mode one slot declares. Free functions, Qt-free. Port of
// dish-android composer/TouchpadRouting.wireMode plus the DS4-mode catalog
// gate from CapabilityResolver.typeOffersFeature; TouchpadModeComposer is the
// thin Observable fold over these.
//
// dish-windows is physical-pads-only, so android's three-way touch source
// (phone screen / pad trackpad / none) collapses to one fact: does the pad
// have a touchpad SDL can read. That single bool gates BOTH rungs, exactly as
// android's controller layer offers TOUCHPAD and MOUSE together iff the slot
// has a touch producer.
//
// The forward PAYLOAD routing (MSG_TOUCHPAD assembly + eventTimeMs) lives in
// TouchpadRouting.h; this header owns only the mode DECISION.

#pragma once

#include "core/model/Protocol.h"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace dish::reducer {

// The three wire strings in canonical order (android TouchpadModeValue.ALL).
// Picker UIs iterate this; the order is pinned so the displayed list is stable
// across builds. Sourced from Protocol.h so the wire vocabulary has one owner.
inline const std::array<std::string_view, 3> kTouchpadModeNames{
    proto::touchpadModeName(proto::kTouchpadModeOff),
    proto::touchpadModeName(proto::kTouchpadModeDs4),
    proto::touchpadModeName(proto::kTouchpadModeMouse),
};

// True iff `name` is exactly one of the three wire strings (case-sensitive —
// the satellite compares verbatim). Round-trips through the proto mappers so
// an unknown string is caught by touchpadModeFromName's collapse-to-off: only
// a genuine wire string survives the there-and-back unchanged.
inline bool isValidTouchpadModeName(std::string_view name) {
    return proto::touchpadModeName(proto::touchpadModeFromName(name)) == name;
}

// The catalog gate for the DS4 pad routing: a type offers it only when its
// touchpad feature is supported AND advertises the "ds4" mode, so a
// touchpad-bearing type with no "ds4" lineage (mouse-only, or a future pad
// with a different mode) gates the pad routing off. A pre-modes catalog omits
// `modes` — empty means pad-capable (back-compat). Mirrors dish-android
// CapabilityResolver.typeOffersFeature's TOUCHPAD arm.
inline bool typeOffersDs4Touchpad(bool supported, const std::vector<std::string>& modes) {
    if (!supported) { return false; }
    if (modes.empty()) { return true; }
    return std::find(modes.begin(), modes.end(),
                     std::string(proto::touchpadModeName(proto::kTouchpadModeDs4))) != modes.end();
}

// The descriptor's touchpadMode for one slot: the per-satellite pick gated by
// what the path can actually carry. DS4 pad routing needs a touch source and a
// type that advertises the mode; mouse routing needs a touch source and a host
// that grants mouse control. A pick the path cannot carry declares "off"
// rather than a request the satellite would dead-letter — and a blocked pick
// NEVER falls back to the other routing (mirrors android wireMode). An empty
// pick is "never picked" and declares off, same as android's null pick.
//
// The host layer never gates the pad routing (it is a per-type concern), and
// the type layer never gates mouse (it is host-injected) — the two rungs read
// disjoint gates on purpose.
inline std::uint8_t resolveTouchpadMode(std::string_view pick, bool padHasTouchpad,
                                        bool typeOffersDs4, bool hostMouseControl) {
    if (pick == proto::touchpadModeName(proto::kTouchpadModeDs4) && padHasTouchpad &&
        typeOffersDs4) {
        return proto::kTouchpadModeDs4;
    }
    if (pick == proto::touchpadModeName(proto::kTouchpadModeMouse) && padHasTouchpad &&
        hostMouseControl) {
        return proto::kTouchpadModeMouse;
    }
    return proto::kTouchpadModeOff;
}

} // namespace dish::reducer
