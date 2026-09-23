// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "MdnsScan.h"

#include "Network/ScopedSocket.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>
#include <chrono>

namespace dish::net {

namespace {

// An ephemeral local port, which is what makes the responders unicast their
// answers back here rather than to the group.
bool bindEphemeral(SOCKET sock) {
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = INADDR_ANY;
    local.sin_port = 0;
    return ::bind(sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != SOCKET_ERROR;
}

// Winsock SO_RCVTIMEO is a DWORD of milliseconds, not a timeval. The short
// timeout is what lets the loop notice its own deadline rather than blocking
// past it; TTL 255 is what mDNS requires of a link-local query.
void applyScanOptions(SOCKET sock) {
    DWORD rcvTimeout = 300;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeout),
                 sizeof(rcvTimeout));
    int ttl = 255;
    ::setsockopt(sock, IPPROTO_IP, IP_MULTICAST_TTL, reinterpret_cast<const char*>(&ttl),
                 sizeof(ttl));
}

void sendQuery(SOCKET sock, const std::vector<std::uint8_t>& query) {
    sockaddr_in dest{};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(kMdnsPort);
    ::inet_pton(AF_INET, kMdnsGroup, &dest.sin_addr);
    ::sendto(sock, reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size()), 0,
             reinterpret_cast<sockaddr*>(&dest), sizeof(dest));
}

} // namespace

void mdnsScan(const std::vector<std::uint8_t>& query, int timeoutMs,
              const std::function<bool(const std::uint8_t*, std::size_t)>& onDatagram) {
    using namespace std::chrono;

    const ScopedSocket sock;
    if (!sock.valid() || !bindEphemeral(sock.get())) { return; }
    applyScanOptions(sock.get());
    sendQuery(sock.get(), query);

    const auto hardDeadline = steady_clock::now() + milliseconds(timeoutMs);
    auto deadline = hardDeadline;
    std::uint8_t buf[2048];

    while (steady_clock::now() < deadline) {
        const int n = ::recvfrom(sock.get(), reinterpret_cast<char*>(buf), sizeof(buf), 0, nullptr,
                                 nullptr);
        if (n <= 0) { continue; } // timeout / transient
        if (!onDatagram(buf, static_cast<std::size_t>(n))) { continue; }
        // Never past the caller's own deadline: the grace window shortens the
        // wait, it does not extend it.
        deadline = std::min(hardDeadline, steady_clock::now() + milliseconds(kMdnsGraceMs));
    }
}

} // namespace dish::net
