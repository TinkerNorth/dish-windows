// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// One HTTP/1.1 request and its response over a socket that is dialled, handshaken, written, read
// and closed on the thread that asked for it, the same transport dish-linux runs. Every answer is
// handed over as it was said, an HTTP error status included; only a transport that produced
// nothing reads as no answer.
//
// The bytes are core/wire/HttpFraming's; this moves them and keeps the deadline.

#pragma once

#include "core/wire/HttpFraming.h"

#include <QByteArray>
#include <QSslConfiguration>
#include <QString>
#include <QUrl>

#include <functional>
#include <optional>
#include <vector>

class QObject;

namespace dish::http {

struct HttpRequest {
    // Host, port, path and query. The port is always spelled out.
    QUrl url;
    QByteArray method;
    std::vector<wire::HttpHeader> headers;
    QByteArray body;
    // Set, the exchange runs TLS under this configuration; unset, plain TCP.
    std::optional<QSslConfiguration> tls;
    // The whole exchange's deadline; 0 waits for as long as the peer takes.
    int timeoutMs = 0;
};

struct HttpResult {
    // Status 0 when nothing answered.
    wire::HttpResponse response;
    // Why nothing answered, in words, for a log line.
    QString error;
};

// Shown the peer's certificate (DER) when the handshake completes, before a byte of the request is
// written; false hangs up. Without one, any certificate is accepted.
using PeerCheck = std::function<bool(const QByteArray& certDer)>;
using HttpDone = std::function<void(const HttpResult&)>;

// Sends `request` and hands its outcome to `done` exactly once, from the event loop and never from
// inside this call. Destroying `owner` first cancels the exchange without calling `done`. The
// exchange is a child of `owner` named for its URL while it runs.
void exchangeHttp(QObject* owner, HttpRequest request, PeerCheck peerCheck, HttpDone done);

} // namespace dish::http
