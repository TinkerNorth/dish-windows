// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The seam both REST clients send their exchanges through. The app's sends each one over its own
// socket on this thread (exchangeHttp); a test answers in-process instead by overriding
// exchange(), which is what lets a manager be driven against satellites at addresses that exist
// nowhere. The exchanges in flight are children of the transport, so destroying the client that
// owns it cancels them.

#pragma once

#include "source/http/HttpExchange.h"

#include <QObject>

namespace dish::http {

class HttpTransport : public QObject {
    Q_OBJECT
  public:
    explicit HttpTransport(QObject* parent = nullptr) : QObject(parent) {}

    virtual void exchange(HttpRequest request, PeerCheck peerCheck, HttpDone done);
};

} // namespace dish::http
