// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "PairingClient.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslSocket>
#include <QString>
#include <QTimer>
#include <QUrl>

namespace dish::net {

namespace {

// Stable translation context for the user-visible fallback text classify()
// produces when the server didn't return an error reason of its own.
constexpr const char* kTrContext = "dish::net::PairingClient";

constexpr int kTimeoutMs = 5000;

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
    return Unreachable{
        response.error.value_or(QCoreApplication::translate(kTrContext, "Server unreachable"))};
}

// Pairing is now POST /api/pair on the satellite's HTTPS client server (it was
// previously a bespoke raw-TCP JSON line-protocol). The request and response
// JSON shapes are unchanged — only the transport differs.
//
// pair() is a blocking call invoked from a QtConcurrent worker thread
// (WifiConnectionManager::pairAndConnect). QNetworkAccessManager is async, so
// we create a manager local to this thread and drive a nested QEventLoop until
// the reply finishes (or a timer trips), keeping the public API blocking.
models::PairResponse PairingClient::pair(const QString& ip, int port, const QString& deviceId,
                                         const QString& deviceName, const QString& pin) {
    const QString url = QStringLiteral("https://%1:%2/api/pair").arg(ip).arg(port);
    const QJsonObject reqObj{{"deviceId", deviceId}, {"deviceName", deviceName}, {"pin", pin}};
    const auto body = QJsonDocument(reqObj).toJson(QJsonDocument::Compact);

    QNetworkRequest req((QUrl(url)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    // The satellite presents a self-signed certificate. By approved project
    // decision the dish accepts it without verification or pinning — the
    // equivalent of `curl --insecure`. Disable both peer (certificate-chain)
    // and host-name verification.
    QSslConfiguration tls = QSslConfiguration::defaultConfiguration();
    tls.setPeerVerifyMode(QSslSocket::VerifyNone);
    req.setSslConfiguration(tls);

    // The manager is stack-local so it (and the reply) belong to this worker
    // thread; the nested event loop below pumps its signals.
    QNetworkAccessManager nam;
    nam.setTransferTimeout(kTimeoutMs);

    QEventLoop loop;
    QNetworkReply* reply = nam.post(req, body);

    // Belt-and-braces: with VerifyNone no sslErrors should fire, but if the
    // platform backend still reports any, swallow them so a self-signed cert
    // never aborts the handshake.
    QObject::connect(reply, &QNetworkReply::sslErrors, reply,
                     [reply](const QList<QSslError>&) { reply->ignoreSslErrors(); });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);

    // setTransferTimeout already bounds the request, but a hard backstop
    // guarantees the loop exits even if the reply never emits finished().
    QTimer guard;
    guard.setSingleShot(true);
    QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
    guard.start(kTimeoutMs + 1000);

    loop.exec();

    if (!reply->isFinished()) {
        reply->abort();
        reply->deleteLater();
        return makeError("connect timeout");
    }

    if (reply->error() != QNetworkReply::NoError) {
        // POST /api/pair always answers HTTP 200 with a JSON body — even on a
        // bad PIN. So any QNetworkReply error here is a genuine transport
        // failure (TLS handshake, connection refused, DNS, timeout); treat it
        // as unreachable, mirroring the old socket-error paths.
        const QByteArray payload = reply->readAll();
        reply->deleteLater();
        if (!payload.isEmpty()) {
            QJsonParseError perr{};
            const auto doc = QJsonDocument::fromJson(payload, &perr);
            if (perr.error == QJsonParseError::NoError && doc.isObject()) {
                return models::PairResponse::fromJson(doc.object());
            }
        }
        return makeError("connect failed");
    }

    const QByteArray payload = reply->readAll();
    reply->deleteLater();
    if (payload.isEmpty()) { return makeError("no response"); }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(payload, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return makeError("malformed response");
    }
    return models::PairResponse::fromJson(doc.object());
}

} // namespace dish::net
