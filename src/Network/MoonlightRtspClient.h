// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Synchronous plaintext RTSP client for the Moonlight handshake. Runs the
// OPTIONS -> DESCRIBE -> SETUP{audio,video,control} -> ANNOUNCE -> PLAY sequence
// over one TCP connection, negotiating the video/audio/control ports at minimal
// settings (we discard media; only the control port and connect-data matter).
//
// Blocking by design: MoonlightSession drives it from a worker thread, off the
// GUI loop. The message formatting/parsing is the pure core/moonlight/Rtsp code;
// this only owns the socket.

#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace dish::net {

// The useful outcome of a successful handshake.
struct RtspHandshakeResult {
    std::uint16_t controlPort = 0;
    std::uint16_t videoPort = 0;
    std::uint16_t audioPort = 0;
    std::uint32_t connectData = 0; // ENet secret from the control SETUP, 0 if none
};

class MoonlightRtspClient {
  public:
    // Runs the full handshake against `host:rtspPort`. `rikeyid` and the display
    // mode ride the ANNOUNCE SDP. Returns the negotiated ports on success.
    // Blocks up to `timeoutMs` per request.
    std::optional<RtspHandshakeResult> handshake(const std::string& host, std::uint16_t rtspPort,
                                                 int width, int height, int fps,
                                                 int timeoutMs = 5000);

  private:
    // One request/response round trip over a fresh connect is not used; Moonlight
    // keeps one socket for the whole sequence.
    std::string lastError_;
};

} // namespace dish::net
