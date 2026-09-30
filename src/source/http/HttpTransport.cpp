// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/http/HttpTransport.h"

#include <utility>

namespace dish::http {

void HttpTransport::exchange(HttpRequest request, PeerCheck peerCheck, HttpDone done) {
    exchangeHttp(this, std::move(request), std::move(peerCheck), std::move(done));
}

} // namespace dish::http
