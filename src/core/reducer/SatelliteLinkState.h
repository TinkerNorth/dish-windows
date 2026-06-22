// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Pure session-presence -> UI LinkState mapper (the chip a connection row
// renders). Pulled out of the composer so the dashboard and the connections
// screen agree by construction and so the rule is unit-testable without
// standing up the whole graph. Qt-free: it takes a presence enum + two booleans
// and returns a LinkState enum, emitting NO localized text. Mirrors
// dish-android composer/ConnectionsComposer.satelliteLinkState (a pure fn that
// happens to live inside the composer file on Android).

#pragma once

namespace dish::reducer {

// The wire-level session presence axis, mirrored Qt-free from
// dish::net::SessionState so this header carries no Qt / Network dependency.
// Idle/Linking/Live/Faltering/Stale match SessionState one-to-one; the manager
// maps its own enum onto this at the call boundary.
//
//  * Idle      — no live session (paired or not).
//  * Linking   — session PUT / auth handshake in flight.
//  * Live      — UDP socket open, heartbeat acks flowing.
//  * Faltering — Live, heartbeat-miss count non-zero, below the death threshold.
//  * Stale     — the session collapsed and the row reads "Needs pairing"
//                (terminal 401 / unpaired close-notify dropped the key).
enum class SessionPresence { Idle, Linking, Live, Faltering, Stale };

// The UI-facing link state, mirrored Qt-free from dish::models::LinkState. The
// mapper emits this; the UI/mapper turns it into a localized chip + glyph.
enum class UiLinkState { Found, Stale, Saved, Ready, Connecting, Connected, Unstable };

// Map the satellite session presence to the UI LinkState. `isStale` is the
// out-of-band "Needs pairing" marker (set on a terminal 401 / unpaired close);
// `isDiscovered` is whether this id is in the current discovery scan. A live or
// linking session ignores both flags (presence wins); only an Idle/Stale
// presence consults them. Mirrors android satelliteLinkState exactly:
//
//   Live      -> Connected
//   Linking   -> Connecting
//   Faltering -> Unstable
//   Idle      -> Stale (isStale) else Ready (isDiscovered) else Saved
//   Stale presence is folded into the Idle arm via isStale (the manager parks a
//   collapsed session in Stale AND marks the id stale, so either path lands
//   "Needs pairing").
inline UiLinkState satelliteLinkState(SessionPresence presence, bool isStale, bool isDiscovered) {
    switch (presence) {
    case SessionPresence::Live:
        return UiLinkState::Connected;
    case SessionPresence::Linking:
        return UiLinkState::Connecting;
    case SessionPresence::Faltering:
        return UiLinkState::Unstable;
    case SessionPresence::Stale:
        // A Stale presence is always "Needs pairing" — it dropped the key.
        return UiLinkState::Stale;
    case SessionPresence::Idle:
    default:
        if (isStale) { return UiLinkState::Stale; }
        if (isDiscovered) { return UiLinkState::Ready; }
        return UiLinkState::Saved;
    }
}

} // namespace dish::reducer
