// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightRtsp.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace dish::moonlight {

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && (std::isspace(static_cast<unsigned char>(s[b])) != 0)) { ++b; }
    while (e > b && (std::isspace(static_cast<unsigned char>(s[e - 1])) != 0)) { --e; }
    return s.substr(b, e - b);
}

// Split into lines, tolerating both CRLF and bare LF.
std::vector<std::string> splitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char ch : text) {
        if (ch == '\n') {
            if (!cur.empty() && cur.back() == '\r') { cur.pop_back(); }
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(ch);
        }
    }
    lines.push_back(cur);
    return lines;
}

std::string lowered(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Offset of the first byte after the header block's terminating blank line, or
// npos while the block is still incomplete. Both CRLFCRLF and LFLF are accepted
// because hosts differ on line endings.
std::size_t headerEnd(const std::string& text) {
    const auto crlf = text.find("\r\n\r\n");
    const auto lf = text.find("\n\n");
    if (crlf != std::string::npos && (lf == std::string::npos || crlf + 4 <= lf + 2)) {
        return crlf + 4;
    }
    if (lf != std::string::npos) { return lf + 2; }
    return std::string::npos;
}

} // namespace

std::string buildRtspRequest(const std::string& command, const std::string& target, int cseq,
                             const std::map<std::string, std::string>& options,
                             const std::string& payload) {
    std::ostringstream os;
    os << command << ' ' << target << " RTSP/1.0\r\n";
    os << "CSeq: " << cseq << "\r\n";
    for (const auto& [key, value] : options) { os << key << ": " << value << "\r\n"; }
    if (!payload.empty()) {
        os << "Content-length: " << payload.size() << "\r\n";
        os << "\r\n";
        os << payload;
    } else {
        os << "\r\n";
    }
    return os.str();
}

std::optional<RtspResponse> parseRtspResponse(const std::string& text) {
    const auto lines = splitLines(text);
    if (lines.empty()) { return std::nullopt; }

    RtspResponse resp;
    // Status line: "RTSP/1.0 200 OK".
    {
        std::istringstream ss(lines.front());
        std::string proto;
        ss >> proto;
        if (proto.rfind("RTSP", 0) != 0) { return std::nullopt; }
        if (!(ss >> resp.statusCode)) { return std::nullopt; }
    }

    std::size_t i = 1;
    bool inPayload = false;
    for (; i < lines.size(); ++i) {
        const std::string& line = lines[i];
        if (!inPayload) {
            if (line.empty()) {
                inPayload = true;
                continue;
            }
            const auto colon = line.find(':');
            if (colon == std::string::npos) { continue; }
            const std::string key = trim(line.substr(0, colon));
            const std::string value = trim(line.substr(colon + 1));
            if (key == "CSeq") {
                resp.cseq = std::atoi(value.c_str());
            } else {
                resp.options[key] = value;
            }
        } else {
            if (line.empty()) { continue; }
            const auto eq = line.find('=');
            if (eq == std::string::npos) {
                resp.payloads.emplace_back(std::string(), trim(line));
            } else {
                resp.payloads.emplace_back(trim(line.substr(0, eq)), trim(line.substr(eq + 1)));
            }
        }
    }
    return resp;
}

std::optional<std::size_t> rtspContentLength(const std::string& text) {
    const std::size_t end = headerEnd(text);
    const std::string block = text.substr(0, end == std::string::npos ? text.size() : end);
    for (const auto& line : splitLines(block)) {
        const auto colon = line.find(':');
        if (colon == std::string::npos) { continue; }
        if (lowered(trim(line.substr(0, colon))) != "content-length") { continue; }
        const std::string value = trim(line.substr(colon + 1));
        if (value.empty()) { return std::nullopt; }
        errno = 0;
        char* stop = nullptr;
        const unsigned long long parsed = std::strtoull(value.c_str(), &stop, 10);
        if (stop == value.c_str() || errno != 0) { return std::nullopt; }
        return static_cast<std::size_t>(parsed);
    }
    return std::nullopt;
}

bool rtspResponseComplete(const std::string& text) {
    const std::size_t end = headerEnd(text);
    if (end == std::string::npos) { return false; }
    const auto declared = rtspContentLength(text);
    if (!declared.has_value()) { return false; }
    return text.size() - end >= *declared;
}

std::string buildAnnounceSdp(int width, int height, int fps) {
    // clientRefreshRateX100 is hundredths of a frame per second.
    constexpr int kFpsHundredths = 100;
    // The floor the protocol lets us ask for. No payload is ever decoded, so the
    // only thing bitrate buys here is host-side encoder work we throw away.
    constexpr int kMinBitrateKbps = 500;

    const std::string attributes[] = {
        "v=0",
        "o=android 0 14 IN IPv4 0.0.0.0",
        "s=NVIDIA Streaming Client",
        "a=x-nv-video[0].clientViewportWd:" + std::to_string(width),
        "a=x-nv-video[0].clientViewportHt:" + std::to_string(height),
        "a=x-nv-video[0].maxFPS:" + std::to_string(fps),
        "a=x-nv-video[0].packetSize:1024",
        "a=x-nv-video[0].rateControlMode:4",
        "a=x-nv-video[0].timeoutLengthMs:7000",
        "a=x-nv-video[0].framesWithInvalidRefThreshold:0",
        "a=x-nv-video[0].refPicInvalidation:0",
        "a=x-nv-video[0].encoderCscMode:0",
        "a=x-nv-video[0].dynamicRangeMode:0",
        "a=x-nv-video[0].maxNumReferenceFrames:1",
        "a=x-nv-video[0].videoEncoderSlicesPerFrame:1",
        "a=x-nv-video[0].clientRefreshRateX100:" + std::to_string(fps * kFpsHundredths),
        "a=x-nv-vqos[0].bitStreamFormat:0",
        "a=x-nv-vqos[0].bw.minimumBitrateKbps:" + std::to_string(kMinBitrateKbps),
        "a=x-nv-vqos[0].bw.maximumBitrateKbps:" + std::to_string(kMinBitrateKbps),
        "a=x-nv-vqos[0].fec.enable:1",
        "a=x-nv-vqos[0].fec.minRequiredFecPackets:2",
        "a=x-nv-vqos[0].fec.repairPercent:20",
        "a=x-nv-vqos[0].drc.enable:0",
        "a=x-nv-vqos[0].videoQualityScoreUpdateTime:5000",
        "a=x-nv-vqos[0].qosTrafficType:5",
        "a=x-nv-aqos.qosTrafficType:4",
        "a=x-nv-aqos.packetDuration:5",
        "a=x-nv-audio.surround.numChannels:2",
        "a=x-nv-audio.surround.channelMask:3",
        "a=x-nv-audio.surround.enable:0",
        "a=x-nv-audio.surround.AudioQuality:0",
        "a=x-nv-general.useReliableUdp:13",
        "a=x-nv-general.featureFlags:167",
        "a=x-ml-general.featureFlags:3",
        "a=x-ss-general.encryptionEnabled:0",
        "t=0 0",
    };

    std::string sdp;
    for (const auto& line : attributes) {
        sdp += line;
        sdp += "\r\n";
    }
    return sdp;
}

std::optional<int> serverPortFromTransport(const std::string& transportValue) {
    // Look for "server_port=" followed by digits.
    const std::string key = "server_port=";
    const auto pos = transportValue.find(key);
    if (pos == std::string::npos) { return std::nullopt; }
    std::size_t p = pos + key.size();
    std::string digits;
    while (p < transportValue.size() &&
           (std::isdigit(static_cast<unsigned char>(transportValue[p])) != 0)) {
        digits.push_back(transportValue[p]);
        ++p;
    }
    if (digits.empty()) { return std::nullopt; }
    return std::atoi(digits.c_str());
}

std::optional<int> setupServerPort(const RtspResponse& response) {
    const auto it = response.options.find("Transport");
    if (it == response.options.end()) { return std::nullopt; }
    return serverPortFromTransport(it->second);
}

std::optional<std::uint32_t> setupConnectData(const RtspResponse& response) {
    const auto it = response.options.find("X-SS-Connect-Data");
    if (it == response.options.end()) { return std::nullopt; }
    const std::string v = trim(it->second);
    if (v.empty()) { return std::nullopt; }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(v.c_str(), &end, 10);
    if (end == v.c_str() || errno != 0) { return std::nullopt; }
    return static_cast<std::uint32_t>(parsed & 0xFFFFFFFFULL);
}

std::string setupPingPayload(const RtspResponse& response) {
    const auto it = response.options.find("X-SS-Ping-Payload");
    if (it == response.options.end()) { return {}; }
    return trim(it->second);
}

} // namespace dish::moonlight
