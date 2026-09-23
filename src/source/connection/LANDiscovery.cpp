// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "LANDiscovery.h"

#include "Network/ScopedSocket.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <chrono>
#include <cstring>

namespace dish::net {

namespace {

// The beacon port is shared: several processes on this machine may be listening for the same
// broadcasts, so the bind must not claim it exclusively. Windows has no SO_REUSEPORT; SO_REUSEADDR
// covers this case.
//
// Winsock SO_RCVTIMEO is a DWORD of milliseconds, not a timeval. The short timeout is what lets the
// loop notice its own deadline rather than blocking past it.
bool bindBeaconPort(SOCKET sock, int port) {
    BOOL reuse = TRUE;
    ::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse),
                 sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        return false;
    }

    DWORD rtv = 300;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rtv), sizeof(rtv));
    return true;
}

// The sender's address as text, empty when it cannot be read. Taken from the datagram rather than
// from the beacon body: a satellite behind NAT, or one that got its own address wrong, is still
// reachable at the address its packet came from.
QString senderAddress(const sockaddr_in& from) {
    char ipStr[INET_ADDRSTRLEN] = {0};
    if (::inet_ntop(AF_INET, &from.sin_addr, ipStr, INET_ADDRSTRLEN) == nullptr) { return {}; }
    return QString::fromLatin1(ipStr);
}

} // namespace

// Listens; it never asks. A satellite broadcasts on its own cadence, so the whole timeout is waited
// out: unlike the mDNS scan there is no query to answer and nothing says more are not coming.
QList<models::DiscoveredServer> LANDiscovery::discover(int port, int timeoutMs) {
    using namespace std::chrono;

    const ScopedSocket sock;
    if (!sock.valid() || !bindBeaconPort(sock.get(), port)) { return {}; }

    const auto deadline = steady_clock::now() + milliseconds(timeoutMs);
    QSet<QString> seen;
    QList<models::DiscoveredServer> result;
    std::uint8_t buf[1024];

    while (steady_clock::now() < deadline) {
        sockaddr_in from{};
        int fl = static_cast<int>(sizeof(from));
        const int n = ::recvfrom(sock.get(), reinterpret_cast<char*>(buf),
                                 static_cast<int>(sizeof(buf)), 0,
                                 reinterpret_cast<sockaddr*>(&from), &fl);
        if (n <= 0) { continue; }
        // One row per address, because a satellite repeats its beacon for as long as the scan runs.
        const QString ip = senderAddress(from);
        if (ip.isEmpty() || seen.contains(ip)) { continue; }
        seen.insert(ip);
        const auto json = QString::fromUtf8(reinterpret_cast<const char*>(buf), n);
        if (auto server = parseBeacon(json, ip)) { result.append(*server); }
    }
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
