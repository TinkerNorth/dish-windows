// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightRtsp.h"

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
    const unsigned long parsed = std::strtoul(v.c_str(), &end, 10);
    if (end == v.c_str() || errno != 0) { return std::nullopt; }
    return static_cast<std::uint32_t>(parsed);
}

} // namespace dish::moonlight
