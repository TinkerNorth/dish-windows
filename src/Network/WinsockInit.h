// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

namespace dish::net {

// RAII wrapper around WSAStartup / WSACleanup. Windows requires every process
// that uses Winsock to call WSAStartup() at least once before any socket(),
// connect(), bind(), etc. and to call WSACleanup() the same number of times
// to release internal resources.
//
// The dish-linux / dish-mac siblings have no equivalent because POSIX sockets
// don't need explicit init. Instantiate a single WinsockInit on the stack of
// `main()` so the lifetime brackets every socket call in the app — including
// LANDiscovery, PairingClient, and the per-session SatelliteClient threads.
class WinsockInit {
  public:
    WinsockInit();
    ~WinsockInit();

    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
    WinsockInit(WinsockInit&&) = delete;
    WinsockInit& operator=(WinsockInit&&) = delete;

    // True if WSAStartup returned 0. The constructor doesn't throw on
    // failure — callers should check and surface a clean error if Winsock
    // wouldn't initialize (essentially impossible on a healthy Windows box).
    bool ok() const { return ok_; }

  private:
    bool ok_ = false;
};

} // namespace dish::net
