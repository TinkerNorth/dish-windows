// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WinsockInit.h"

#include <winsock2.h>

namespace dish::net {

WinsockInit::WinsockInit() {
    WSADATA data{};
    // 2.2 is the only version anyone has shipped in this millennium and what
    // every Windows since XP supports; matching the satellite server.
    const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
    ok_ = (rc == 0);
}

WinsockInit::~WinsockInit() {
    if (ok_) { ::WSACleanup(); }
}

} // namespace dish::net
