// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The one definition of the test-only friend seam declared in
// Network/MoonlightSession.h, shared by every test TU that needs it (ODR), in
// the same shape as satellite_client_test_access.h.
//
// It feeds the session the world signals a live host raises. A control stream
// that connected, dropped or was terminated arrives from the ENet receive thread
// and cannot be produced without a host, so without this seam the difference
// between "a drop tells the host nothing" and "a drop closes the app" could only
// ever be asserted by reading the reducer. Everything downstream of the feed is
// real: the reducer runs, the effects run, and the requests they make are the
// ones MoonlightRequestLog observes.
//
// `settle` is setup and not behaviour. Reaching Streaming honestly needs an RTSP
// handshake against a host, so the phase is placed and the event under test is
// then fed for real.

#pragma once

#include "Network/MoonlightSession.h"
#include "core/moonlight/MoonlightSessionMachine.h"

#include <array>
#include <cstdint>
#include <string>

namespace dish::net {

class MoonlightSessionTestAccess {
  public:
    // Place the session in a phase, running no effects. Setup only.
    static void settle(MoonlightSession& session, moonlight::SessionPhase phase,
                       moonlight::SessionFailure failure = moonlight::SessionFailure::None) {
        session.state_ = moonlight::SessionState{phase, failure};
    }

    // Feed one world signal through the real reducer and run the real effects.
    static void feed(MoonlightSession& session, moonlight::SessionEvent event) {
        session.dispatch(event);
    }

    // Hand the session a /launch reply it would otherwise have to be given by a
    // host, so the silent-resume branch and the two refusals can be exercised
    // against the code that actually reads them.
    static void feedLaunchReply(MoonlightSession& session, const MoonlightXmlResponse& reply,
                                bool resuming) {
        session.onLaunchReply(reply, resuming);
    }

    // Whether anything this session does can still reach the store.
    static bool persists(const MoonlightSession& session) { return session.repo_ != nullptr; }

    // Open the control link to `ip`:`port` under `key`, the call the RTSP worker
    // makes once SETUP has named the port, and report the link up the way the
    // worker does. Setup only: the handshake that precedes it needs a host.
    static bool connectControl(MoonlightSession& session, const std::string& ip, std::uint16_t port,
                               const std::array<std::uint8_t, 16>& key) {
        settle(session, moonlight::SessionPhase::ControlConnecting);
        const bool connected = session.control_.connect(ip, port, key, 0);
        if (connected) { feed(session, moonlight::SessionEvent::ControlConnected); }
        return connected;
    }
};

} // namespace dish::net
