// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "PairingClient.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

namespace dish::net {

namespace {

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

models::PairResponse makeError(const char* msg) {
    models::PairResponse r;
    r.ok = false;
    r.error = QString::fromLatin1(msg);
    // Synthesized network-error responses are unreachable by construction —
    // we never made it far enough to receive a JSON body. fromJson flips this
    // to true on the success path.
    r.reachable = false;
    return r;
}

} // namespace

PairingClient::Outcome PairingClient::classify(const models::PairResponse& response) {
    if (response.ok && response.sharedKey.has_value() && !response.sharedKey->isEmpty()) {
        return Success{*response.sharedKey};
    }
    if (response.reachable) { return AuthRequired{}; }
    return Unreachable{response.error.value_or(QStringLiteral("Server unreachable"))};
}

models::PairResponse PairingClient::pair(const QString& ip, int port, const QString& deviceId,
                                         const QString& deviceName, const QString& pin) {
    const SOCKET sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == INVALID_SOCKET) { return makeError("socket failed"); }

    // Put the socket in non-blocking mode for the connect() + select() pair.
    // Winsock equivalent of fcntl(O_NONBLOCK) is ioctlsocket(FIONBIO).
    u_long nonBlocking = 1;
    ::ioctlsocket(sock, FIONBIO, &nonBlocking);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    if (::inet_pton(AF_INET, ip.toUtf8().constData(), &addr.sin_addr) != 1) {
        ::closesocket(sock);
        return makeError("bad ip");
    }

    const int connectRet = ::connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    // Winsock returns SOCKET_ERROR + WSAGetLastError() == WSAEWOULDBLOCK for
    // an in-progress non-blocking connect. POSIX EINPROGRESS is the same
    // semantic; the macro is just named differently.
    if (connectRet == SOCKET_ERROR && ::WSAGetLastError() != WSAEWOULDBLOCK) {
        ::closesocket(sock);
        return makeError("connect failed");
    }
    if (connectRet == SOCKET_ERROR) {
        timeval tv{};
        tv.tv_sec = 4;
        fd_set wset;
        FD_ZERO(&wset);
        FD_SET(sock, &wset);
        // Winsock select() ignores its nfds argument — pass 0 for clarity.
        const int sel = ::select(0, nullptr, &wset, nullptr, &tv);
        if (sel <= 0) {
            ::closesocket(sock);
            return makeError("connect timeout");
        }
        int sockerr = 0;
        int sl = static_cast<int>(sizeof(sockerr));
        ::getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&sockerr), &sl);
        if (sockerr != 0) {
            ::closesocket(sock);
            return makeError("connect refused");
        }
    }
    // Restore blocking mode so the send/recv pair is straightforward.
    u_long blocking = 0;
    ::ioctlsocket(sock, FIONBIO, &blocking);

    const QJsonObject reqObj{{"deviceId", deviceId}, {"deviceName", deviceName}, {"pin", pin}};
    const auto body = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);
    if (::send(sock, body.constData(), static_cast<int>(body.size()), MSG_NOSIGNAL) ==
        SOCKET_ERROR) {
        ::closesocket(sock);
        return makeError("send failed");
    }

    // Winsock SO_RCVTIMEO is a DWORD of milliseconds.
    DWORD rtv = 5000;
    ::setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rtv), sizeof(rtv));

    char buf[512];
    const int n = ::recv(sock, buf, static_cast<int>(sizeof(buf)), 0);
    ::closesocket(sock);
    if (n <= 0) { return makeError("no response"); }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(QByteArray(buf, n), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return makeError("malformed response");
    }
    return models::PairResponse::fromJson(doc.object());
}

} // namespace dish::net
