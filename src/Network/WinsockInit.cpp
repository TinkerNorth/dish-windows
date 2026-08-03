// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "WinsockInit.h"

#include <winsock2.h>

namespace dish::net {

WinsockInit::WinsockInit() {
    WSADATA data{};
    const int rc = ::WSAStartup(MAKEWORD(2, 2), &data);
    ok_ = (rc == 0);
}

WinsockInit::~WinsockInit() {
    if (ok_) { ::WSACleanup(); }
}

} // namespace dish::net
