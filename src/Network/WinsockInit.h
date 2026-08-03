// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

namespace dish::net {

// RAII pairing of WSAStartup / WSACleanup, which Winsock requires per process
// before any socket call and in matching counts. Lives on `main()`'s stack so
// its lifetime brackets every socket in the app, including the per-session
// SatelliteClient threads.
class WinsockInit {
  public:
    WinsockInit();
    ~WinsockInit();

    WinsockInit(const WinsockInit&) = delete;
    WinsockInit& operator=(const WinsockInit&) = delete;
    WinsockInit(WinsockInit&&) = delete;
    WinsockInit& operator=(WinsockInit&&) = delete;

    // The constructor does not throw, so callers must check this.
    bool ok() const { return ok_; }

  private:
    bool ok_ = false;
};

} // namespace dish::net
