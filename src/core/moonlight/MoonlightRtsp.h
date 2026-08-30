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
//
// Framing lives here too, because a Moonlight host frames a reply two different
// ways: with a Content-length, or by hanging up. rtspResponseComplete answers
// the first case; the caller reads to EOF for the second.

#pragma once

#include <cstddef>
#include <cstdint>
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

// The Content-length a response header block declares, if it declares one. The
// name is matched case-insensitively: hosts spell it both ways.
std::optional<std::size_t> rtspContentLength(const std::string& text);

// Whether `text` already holds a whole response: the header block terminated by
// a blank line, plus the body its Content-length declares. A reply that names no
// Content-length is framed by the host hanging up instead, so this stays false
// for it and the caller must read to end-of-stream.
bool rtspResponseComplete(const std::string& text);

// The ANNOUNCE session description. THE WHOLE ATTRIBUTE SET IS REQUIRED: a host
// builds its stream configuration by looking each attribute up by name, and a
// lookup that misses is fatal. Measured against a live Sunshine host, an
// ANNOUNCE carrying only the handful of attributes this client cares about is
// answered 400 BAD REQUEST while this set is answered 200 OK. Nothing here is
// decoration.
std::string buildAnnounceSdp(int width, int height, int fps);

// Extract the numeric `server_port=N` from a SETUP response's Transport option.
// nullopt when absent or unparseable.
std::optional<int> serverPortFromTransport(const std::string& transportValue);

// Convenience over the two above: the server_port from a SETUP response.
std::optional<int> setupServerPort(const RtspResponse& response);

// The X-SS-Connect-Data value (the ENet connect secret) from a control SETUP
// response, when the host supplies it. nullopt otherwise.
//
// READ WIDE, THEN NARROW. The token is unsigned 32-bit and a real host's
// routinely sits above INT32_MAX (4270471497 came off a live Sunshine host), so
// a signed parse yields nothing and the control stream connects with a token of
// zero. Parsed as 64-bit and narrowed to the 32 bits ENet puts on the wire.
std::optional<std::uint32_t> setupConnectData(const RtspResponse& response);

// The X-SS-Ping-Payload from an audio or video SETUP response: the host's
// per-session media-ping secret. It LOOKS like hex but is 16 printable ASCII
// bytes that go back verbatim inside the SS_PING datagram; hex-decoding it
// produces a length the host silently drops. Empty when the host named none.
std::string setupPingPayload(const RtspResponse& response);

} // namespace dish::moonlight
