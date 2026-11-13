// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A Steam Controller slot's trackpad pick, `stick | pad | mouse | off`, and the
// ladder that turns the stored pick into the one in force and into the
// `touchpadMode` the slot declares. One pick covers both pads, so `pad` on the
// 2015 model gives up its right stick. TouchpadModeResolve.h owns the
// per-satellite pick every other pad rides; this owns only the Steam slots'.

#pragma once

#include "core/input/SteamTrackpads.h"
#include "core/model/Protocol.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace dish::reducer {

using input::usbparse::SteamTrackpads;
using input::usbparse::TrackpadPick;

// Picker UIs iterate this, so the order is pinned: the enum's own.
inline constexpr std::array<std::string_view, 4> kTrackpadPickNames{"stick", "pad", "mouse", "off"};

inline constexpr std::string_view trackpadPickName(TrackpadPick pick) {
    switch (pick) {
    case TrackpadPick::Stick:
        return kTrackpadPickNames[0];
    case TrackpadPick::Pad:
        return kTrackpadPickNames[1];
    case TrackpadPick::Mouse:
        return kTrackpadPickNames[2];
    case TrackpadPick::Off:
        return kTrackpadPickNames[3];
    }
    return kTrackpadPickNames[3];
}

// Case-sensitive, like the touchpad mode strings: only a genuine name parses.
inline std::optional<TrackpadPick> trackpadPickFromName(std::string_view name) {
    constexpr std::array<TrackpadPick, 4> kPicks{TrackpadPick::Stick, TrackpadPick::Pad,
                                                 TrackpadPick::Mouse, TrackpadPick::Off};
    for (const TrackpadPick pick : kPicks) {
        if (trackpadPickName(pick) == name) { return pick; }
    }
    return std::nullopt;
}

inline bool isValidTrackpadPickName(std::string_view name) {
    return trackpadPickFromName(name).has_value();
}

// The picks a model offers: the 2026 pad has a real right stick, so `stick` is
// not one of its.
inline std::vector<TrackpadPick> trackpadPickChoices(SteamTrackpads model) {
    switch (model) {
    case SteamTrackpads::None:
        return {};
    case SteamTrackpads::Pad2015:
        return {TrackpadPick::Stick, TrackpadPick::Pad, TrackpadPick::Mouse, TrackpadPick::Off};
    case SteamTrackpads::Pad2026:
        return {TrackpadPick::Pad, TrackpadPick::Mouse, TrackpadPick::Off};
    }
    return {};
}

// What a picker lists: the model's choices, without `mouse` while this client
// cannot route a pad as the host's mouse (TouchpadModeResolve.h's deferral).
inline std::vector<TrackpadPick> offeredTrackpadPicks(SteamTrackpads model, bool mouseAvailable) {
    std::vector<TrackpadPick> offered = trackpadPickChoices(model);
    if (mouseAvailable) { return offered; }
    offered.erase(std::remove(offered.begin(), offered.end(), TrackpadPick::Mouse), offered.end());
    return offered;
}

// Out of the box: the 2015 pad keeps its right pad as the right stick; the 2026
// pad puts its pads on the surface when the emulated type has one, else nowhere.
inline TrackpadPick defaultTrackpadPick(SteamTrackpads model, bool typeOffersDs4) {
    switch (model) {
    case SteamTrackpads::None:
        return TrackpadPick::Off;
    case SteamTrackpads::Pad2015:
        return TrackpadPick::Stick;
    case SteamTrackpads::Pad2026:
        return typeOffersDs4 ? TrackpadPick::Pad : TrackpadPick::Off;
    }
    return TrackpadPick::Off;
}

// The pick in force: the stored one where the model offers it, else the model's
// default, then held to what the path can carry. A pad surface needs the
// emulated type to have one and mouse needs the host's grant; either falls to
// off, never to a surface the satellite would dead-letter.
inline TrackpadPick effectiveTrackpadPick(const std::optional<std::string>& stored,
                                          SteamTrackpads model, bool typeOffersDs4,
                                          bool mouseAvailable) {
    const std::optional<TrackpadPick> parsed =
        stored.has_value() ? trackpadPickFromName(*stored) : std::nullopt;
    const std::vector<TrackpadPick> choices = trackpadPickChoices(model);
    const bool offered =
        parsed.has_value() && std::find(choices.begin(), choices.end(), *parsed) != choices.end();
    const TrackpadPick wanted = offered ? *parsed : defaultTrackpadPick(model, typeOffersDs4);
    switch (wanted) {
    case TrackpadPick::Stick:
        return TrackpadPick::Stick;
    case TrackpadPick::Pad:
        return typeOffersDs4 ? TrackpadPick::Pad : TrackpadPick::Off;
    case TrackpadPick::Mouse:
        return mouseAvailable ? TrackpadPick::Mouse : TrackpadPick::Off;
    case TrackpadPick::Off:
        return TrackpadPick::Off;
    }
    return TrackpadPick::Off;
}

// The descriptor's touchpadMode for a pick: the pads on the surface are the DS4
// pad mode, the pads as a mouse the mouse mode, and the stick and off carry no
// surface.
inline std::uint8_t wireModeForTrackpadPick(TrackpadPick pick) {
    switch (pick) {
    case TrackpadPick::Stick:
    case TrackpadPick::Off:
        return proto::kTouchpadModeOff;
    case TrackpadPick::Pad:
        return proto::kTouchpadModeDs4;
    case TrackpadPick::Mouse:
        return proto::kTouchpadModeMouse;
    }
    return proto::kTouchpadModeOff;
}

} // namespace dish::reducer
