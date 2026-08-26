// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Synchronous plaintext RTSP client for the Moonlight handshake. Runs the
// OPTIONS -> DESCRIBE -> SETUP{audio,video,control} -> ANNOUNCE -> PLAY sequence
// against the host's RTSP port, negotiating the video/audio/control ports (we
// discard media; only the control port, the connect token and the media ping
// payload matter).
//
// ONE CONNECTION PER MESSAGE, and it has to be. A Moonlight host answers exactly
// one RTSP message per TCP connection and then hangs up on its own; a second
// message written into that socket is never seen at all. Measured against a live
// Sunshine host, reusing the socket cost the whole stream setup, which failed at
// DESCRIBE with the host already gone. So each request opens its own socket and
// closes it.
//
// The reply is framed by that hang-up as much as by Content-length: the DESCRIBE
// answer carries no length header and is terminated by the close, so a reply
// without one is read to end-of-stream.
//
// Blocking by design: MoonlightSession drives it from a worker thread, off the
// GUI loop. The message formatting/parsing/framing is the pure
// core/moonlight/Rtsp code; this only owns the sockets and the CSeq counter.

#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace dish::moonlight {
struct RtspResponse;
}

namespace dish::net {

// The useful outcome of a successful handshake.
struct RtspHandshakeResult {
    std::uint16_t controlPort = 0;
    std::uint16_t videoPort = 0;
    std::uint16_t audioPort = 0;
    std::uint32_t connectData = 0; // ENet secret from the control SETUP, 0 if none
    // X-SS-Ping-Payload from the video/audio SETUP responses (16 chars when the
    // host supports the session-id extension, empty otherwise). The RTP client
    // ping carries it so the host can match the datagram to this session.
    std::string videoPingPayload;
    std::string audioPingPayload;
};

class MoonlightRtspClient {
  public:
    MoonlightRtspClient(std::string host, std::uint16_t rtspPort, int timeoutMs = 5000);

    // Runs the full handshake. The display mode rides the ANNOUNCE SDP. Returns
    // the negotiated ports on success, nullopt on the first step that fails.
    std::optional<RtspHandshakeResult> handshake(int width, int height, int fps);

    // The step the handshake died on, as it appears in the log ("SETUP video
    // (CSeq 4)"). A host that hangs up mid-handshake reaches us as a bare write
    // or read failure with no reply attached, so the step is the only thing that
    // identifies it.
    const std::string& lastStage() const { return stage_; }

  private:
    // One request over one socket: connect, ask, read the answer, close.
    std::optional<moonlight::RtspResponse> send(const std::string& command,
                                                const std::string& target,
                                                const std::map<std::string, std::string>& options,
                                                const std::string& payload = {});
    std::optional<moonlight::RtspResponse> setup(const std::string& streamId);
    int nextCseq() { return ++cseq_; }

    std::string host_;
    std::uint16_t rtspPort_;
    int timeoutMs_;
    int cseq_ = 0;
    std::string stage_ = "connect";
};

} // namespace dish::net
