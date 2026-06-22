// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure connection-row shaping: glyph / dot-color / status-chip / "is this link
// live" derivation from a UiLinkState, plus the row label/detail composition as
// STRING KEYS + ARGS rather than localized text. This is the fix for android's
// one architectural wart — ConnectionsComposer reaches Context.getString to
// localize the row detail; here the composer stays Qt-free and emits a
// RowDetailKey + the (ip, udpPort) args, and the UI layer turns that into the
// localized string. Mirrors dish-android ui/common/ConnectionGlyphs.kt
// (glyphForConnection / dotColorForState / statusChipTextRes) +
// ui/connections/ConnectionRows.kt (paintConnection) + ControllerAdapter
// (isLiveLink), with the localization pushed up to the UI per the SoC rule "a
// Composer never touches tr()/widget types — emit keys, localize in the UI".

#pragma once

#include "core/reducer/SatelliteLinkState.h"

namespace dish::reducer {

// Which kind of connection a row represents. Windows is physical-controllers-
// only, so only Satellite is wired here; the Bluetooth-HID-peripheral kind
// android carries is phone-only and out of scope. The enum is kept open so the
// glyph/chip mappers below read like the android table.
enum class ConnectionKind { Satellite };

// The glyph (icon) a row shows, as a stable key the UI resolves to a Qt icon /
// resource. Mirrors android glyphForConnection(SATELLITE, state): Connected ->
// the "connected" satellite, Saved/Stale -> the "off" satellite, everything
// else -> the base satellite glyph.
enum class ConnectionGlyph { SatelliteBase, SatelliteConnected, SatelliteOff };

// The status-dot tint, as a semantic color token (resolved to a Theme color in
// the UI). Mirrors android dotColorForState: Connected -> success,
// Connecting/Unstable -> primary, Stale -> warning, the rest -> muted.
enum class DotColor { Success, Primary, Warning, Muted };

// The status-chip text, as a key the UI localizes (the cross-client chip
// vocabulary). One key per LinkState; mirrors android statusChipTextRes.
enum class StatusChipKey { Found, NeedsPairing, Offline, Ready, Connecting, Online, Unstable };

// The row's detail line, as a key + args (NOT localized text). The UI formats
// "<ip> • UDP <port>" from the args under its own .ts catalog. This is the
// seam that keeps the composer Qt/tr()-free.
enum class RowDetailKey { DiscoveredRow };

// True iff the link is actively streaming. A Faltering (Unstable) link is still
// streaming, so it counts as live everywhere on the dashboard exactly as the
// connections screen treats it. Mirrors android LinkState.isLiveLink().
inline bool isLiveLink(UiLinkState state) {
    return state == UiLinkState::Connected || state == UiLinkState::Unstable;
}

inline ConnectionGlyph glyphForConnection(ConnectionKind kind, UiLinkState state) {
    switch (kind) {
    case ConnectionKind::Satellite:
    default:
        switch (state) {
        case UiLinkState::Connected:
            return ConnectionGlyph::SatelliteConnected;
        case UiLinkState::Saved:
        case UiLinkState::Stale:
            return ConnectionGlyph::SatelliteOff;
        default:
            return ConnectionGlyph::SatelliteBase;
        }
    }
}

inline DotColor dotColorForState(UiLinkState state) {
    switch (state) {
    case UiLinkState::Connected:
        return DotColor::Success;
    case UiLinkState::Connecting:
    case UiLinkState::Unstable:
        return DotColor::Primary;
    case UiLinkState::Stale:
        return DotColor::Warning;
    case UiLinkState::Found:
    case UiLinkState::Saved:
    case UiLinkState::Ready:
    default:
        return DotColor::Muted;
    }
}

inline StatusChipKey statusChipKey(UiLinkState state) {
    switch (state) {
    case UiLinkState::Found:
        return StatusChipKey::Found;
    case UiLinkState::Stale:
        return StatusChipKey::NeedsPairing;
    case UiLinkState::Saved:
        return StatusChipKey::Offline;
    case UiLinkState::Ready:
        return StatusChipKey::Ready;
    case UiLinkState::Connecting:
        return StatusChipKey::Connecting;
    case UiLinkState::Connected:
        return StatusChipKey::Online;
    case UiLinkState::Unstable:
    default:
        return StatusChipKey::Unstable;
    }
}

} // namespace dish::reducer
