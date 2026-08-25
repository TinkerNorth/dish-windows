// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/connection/NvstreamDiscovery.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <QSet>

#include <algorithm>
#include <chrono>
#include <cstring>

namespace dish::net {

namespace {

constexpr const char* kMulticastGroup = "224.0.0.251";
constexpr std::uint16_t kMulticastPort = 5353;
constexpr std::uint16_t kTypeA = 1;
constexpr std::uint16_t kTypePtr = 12;
constexpr std::uint16_t kTypeSrv = 33;
constexpr std::uint16_t kClassInQu = 0x8001;
constexpr int kGraceMs = 600;

std::uint16_t read16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((p[0] << 8) | p[1]);
}

// The PTR query for `_nvstream._tcp.local.`.
std::vector<std::uint8_t> buildQuery() {
    std::vector<std::uint8_t> q;
    const std::uint8_t header[12] = {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0};
    q.insert(q.end(), header, header + 12);
    for (const char* label : {"_nvstream", "_tcp", "local"}) {
        const auto n = static_cast<std::uint8_t>(std::strlen(label));
        q.push_back(n);
        q.insert(q.end(), label, label + n);
    }
    q.push_back(0);
    q.push_back(static_cast<std::uint8_t>(kTypePtr >> 8));
    q.push_back(static_cast<std::uint8_t>(kTypePtr & 0xFF));
    q.push_back(static_cast<std::uint8_t>(kClassInQu >> 8));
    q.push_back(static_cast<std::uint8_t>(kClassInQu & 0xFF));
    return q;
}

} // namespace

namespace nvstream_detail {

std::size_t skipName(const std::uint8_t* p, std::size_t len, std::size_t off) {
    std::size_t consumed = 0;
    bool jumped = false;
    std::size_t guard = 0;
    while (off < len) {
        if (++guard > len) { return 0; }
        const std::uint8_t b = p[off];
        if (b == 0) {
            if (!jumped) { consumed += 1; }
            return consumed;
        }
        if ((b & 0xC0) == 0xC0) {
            if (off + 1 >= len) { return 0; }
            if (!jumped) { consumed += 2; }
            const std::size_t target = (static_cast<std::size_t>(b & 0x3F) << 8) | p[off + 1];
            if (target >= off) { return 0; }
            off = target;
            jumped = true;
            continue;
        }
        const std::size_t label = b + 1;
        if (off + label > len) { return 0; }
        if (!jumped) { consumed += label; }
        off += label;
    }
    return 0;
}

bool readName(const std::uint8_t* p, std::size_t len, std::size_t off, std::string& out) {
    out.clear();
    std::size_t guard = 0;
    while (off < len) {
        if (++guard > len) { return false; }
        const std::uint8_t b = p[off];
        if (b == 0) { return true; }
        if ((b & 0xC0) == 0xC0) {
            if (off + 1 >= len) { return false; }
            const std::size_t target = (static_cast<std::size_t>(b & 0x3F) << 8) | p[off + 1];
            if (target >= off) { return false; }
            off = target;
            continue;
        }
        if (off + 1 + b > len) { return false; }
        if (!out.empty()) { out += '.'; }
        out.append(reinterpret_cast<const char*>(p + off + 1), b);
        off += b + 1;
    }
    return false;
}

std::optional<models::MoonlightHost> parseResponse(const std::uint8_t* p, std::size_t len) {
    if (len < 12) { return std::nullopt; }
    const std::uint16_t qd = read16(p + 4);
    const std::uint16_t an = read16(p + 6);
    std::size_t pos = 12;

    for (std::uint16_t i = 0; i < qd; ++i) {
        const std::size_t consumed = skipName(p, len, pos);
        if (consumed == 0) { return std::nullopt; }
        pos += consumed + 4;
        if (pos > len) { return std::nullopt; }
    }

    std::string ip;
    std::string instance;
    bool haveSrv = false;

    for (std::uint16_t i = 0; i < an; ++i) {
        const std::size_t nameLen = skipName(p, len, pos);
        if (nameLen == 0) { return std::nullopt; }
        pos += nameLen;
        if (pos + 10 > len) { return std::nullopt; }
        const std::uint16_t type = read16(p + pos);
        const std::uint16_t rdlen = read16(p + pos + 8);
        const std::size_t rdata = pos + 10;
        if (rdata + rdlen > len) { return std::nullopt; }

        if (type == kTypeA && rdlen == 4) {
            char buf[INET_ADDRSTRLEN] = {};
            in_addr a{};
            std::memcpy(&a, p + rdata, 4);
            if (::inet_ntop(AF_INET, &a, buf, sizeof(buf)) != nullptr) { ip = buf; }
        } else if (type == kTypeSrv && rdlen >= 7) {
            haveSrv = true;
        } else if (type == kTypePtr && instance.empty()) {
            std::string n;
            if (readName(p, len, rdata, n)) { instance = n.substr(0, n.find('.')); }
        }
        pos = rdata + rdlen;
    }

    if (ip.empty() || !haveSrv) { return std::nullopt; }
    return nvstreamServiceToHost(QString::fromStdString(instance), QString::fromStdString(ip));
}

} // namespace nvstream_detail

std::optional<models::MoonlightHost> nvstreamServiceToHost(const QString& instanceName,
                                                           const QString& hostAddress) {
    if (hostAddress.isEmpty()) { return std::nullopt; }
    models::MoonlightHost h;
    h.name = instanceName.isEmpty() ? hostAddress : instanceName;
    h.ip = hostAddress;
    // The two Moonlight ports are fixed; everything else is learned at runtime.
    h.httpPort = models::kMoonlightHttpPort;
    h.httpsPort = models::kMoonlightHttpsPort;
    h.discovered = true;
    return h;
}

QList<models::MoonlightHost> NvstreamDiscovery::discover(int timeoutMs) {
    using namespace std::chrono;

    const SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { return {}; }

    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    if (::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) == SOCKET_ERROR) {
        ::closesocket(sock);
        return {};
    }

    DWORD rcvTimeout = 300;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
                 sizeof(rcvTimeout));
    int ttl = 255;
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl),
                 sizeof(ttl));

    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kMulticastPort);
    ::inet_pton(AF_INET, kMulticastGroup, &dest.sin_addr);

    const auto query = buildQuery();
    ::sendto(sock, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
             reinterpret_cast<sockaddr*>(&dest), sizeof(dest));

    QList<models::MoonlightHost> result;
    QSet<QString> seen;
    const auto hardDeadline = steady_clock::now() + milliseconds(timeoutMs);
    auto deadline = hardDeadline;
    std::uint8_t buf[2048];

    while (steady_clock::now() < deadline) {
        const int n =
            ::recvfrom(sock, reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr, nullptr);
        if (n <= 0) { continue; }
        const auto host = nvstream_detail::parseResponse(buf, static_cast<std::size_t>(n));
        if (!host) { continue; }
        if (seen.contains(host->ip)) { continue; }
        seen.insert(host->ip);
        result.append(*host);
        deadline = std::min(hardDeadline, steady_clock::now() + milliseconds(kGraceMs));
    }

    ::closesocket(sock);
    return result;
}

} // namespace dish::net
