// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Plaintext RTSP request formatting and response parsing for the Moonlight
// handshake (OPTIONS -> DESCRIBE -> SETUP{audio,video,control} -> ANNOUNCE ->
// PLAY). Message shapes follow rtsp.adoc and the Wolf reference parser; nothing
// is copied from GPL sources.
//
// Pure text in / text out: the socket lives in Network/MoonlightRtspClient. We
// only care about the control stream, so video/audio config is announced at
// minimal settings and the SETUP `server_port` values are the useful output.

#pragma once

#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace dish::moonlight {

// A parsed RTSP response.
struct RtspResponse {
    int statusCode = 0;
    int cseq = 0;
    // Header option lines (key -> value), keys as sent.
    std::map<std::string, std::string> options;
    // Payload lines after the blank line: (key, value) pairs of `key=value` or
    // `a=...` SDP-style attributes.
    std::vector<std::pair<std::string, std::string>> payloads;
};

// Build an RTSP request. `options` are emitted as "Key: value" lines after the
// mandatory "CSeq: n"; `payload`, when non-empty, follows the blank line. Lines
// are CRLF-terminated.
std::string buildRtspRequest(const std::string& command, const std::string& target, int cseq,
                             const std::map<std::string, std::string>& options = {},
                             const std::string& payload = {});

// Parse an RTSP response. nullopt when the message is not a well-formed response
// (no "RTSP/1.0 <code>" status line).
std::optional<RtspResponse> parseRtspResponse(const std::string& text);

// Extract the numeric `server_port=N` from a SETUP response's Transport option.
// nullopt when absent or unparseable.
std::optional<int> serverPortFromTransport(const std::string& transportValue);

// Convenience over the two above: the server_port from a SETUP response.
std::optional<int> setupServerPort(const RtspResponse& response);

// The X-SS-Connect-Data value (the ENet connect secret) from a control SETUP
// response, when the host supplies it. nullopt otherwise.
std::optional<std::uint32_t> setupConnectData(const RtspResponse& response);

} // namespace dish::moonlight
