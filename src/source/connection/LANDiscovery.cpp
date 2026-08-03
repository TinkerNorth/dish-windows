// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "LANDiscovery.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>

namespace dish::net {

QList<models::DiscoveredServer> LANDiscovery::discover(int port, int timeoutMs) {
    using namespace std::chrono;

    const SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) { return {}; }

    BOOL reuse = TRUE;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));
    // Windows has no SO_REUSEPORT; SO_REUSEADDR covers the same case here
    // (several processes listening on one UDP broadcast port).

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(sock);
        return {};
    }

    // Winsock SO_RCVTIMEO is a DWORD of milliseconds, not a timeval.
    DWORD rtv = 300;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rtv), sizeof(rtv));

    const auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    QSet<QString> seen;
    QList<models::DiscoveredServer> result;
    std::uint8_t buf[1024];

    while (steady_clock::now() < deadline) {
        sockaddr_in from{};
        int fl = static_cast<int>(sizeof(from));
        const int n = ::recvfrom(sock, reinterpret_cast<char*>(buf), static_cast<int>(sizeof(buf)),
                                 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) { continue; }
        const auto json = QString::fromUtf8(reinterpret_cast<const char*>(buf), n);
        char ipStr[INET_ADDRSTRLEN] = {0};
        ::inet_ntop(AF_INET, &from.sin_addr, ipStr, INET_ADDRSTRLEN);
        const QString ip = QString::fromLatin1(ipStr);
        if (seen.contains(ip)) { continue; }
        seen.insert(ip);
        if (auto server = parseBeacon(json, ip)) { result.append(*server); }
    }

    ::closesocket(sock);
    return result;
}

std::optional<models::DiscoveredServer> LANDiscovery::parseBeacon(const QString& json,
                                                                  const QString& observedIp) {
    if (!json.contains(QStringLiteral("\"service\":\"satellite\""))) { return std::nullopt; }
    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) { return std::nullopt; }
    auto server = models::DiscoveredServer::fromJson(doc.object());
    server.ip = observedIp;
    if (server.name.isEmpty()) { return std::nullopt; }
    return server;
}

} // namespace dish::net
