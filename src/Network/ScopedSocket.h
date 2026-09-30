// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A UDP socket that closes when it goes out of scope.
//
// For sockets whose lifetime IS a scope: a discovery scan opens one, uses it for
// a few seconds and is done with it, and every early return in between had to
// repeat a closesocket that nothing enforced. SatelliteClient's socket is not
// one of these - it lives as long as the client does and is closed on a state
// change, not on a return - so it keeps its own handle.
//
// Non-copyable and non-movable: one owner, one close.

#pragma once

#include <winsock2.h>

namespace dish::net {

class ScopedSocket {
  public:
    ScopedSocket() : sock_(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {}
    ~ScopedSocket() {
        if (sock_ != INVALID_SOCKET) { ::closesocket(sock_); }
    }
    ScopedSocket(const ScopedSocket&) = delete;
    ScopedSocket& operator=(const ScopedSocket&) = delete;
    ScopedSocket(ScopedSocket&&) = delete;
    ScopedSocket& operator=(ScopedSocket&&) = delete;

    bool valid() const { return sock_ != INVALID_SOCKET; }
    SOCKET get() const { return sock_; }

  private:
    SOCKET sock_;
};

} // namespace dish::net
