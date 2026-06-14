// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QByteArray>
#include <QObject>
#include <QString>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;

namespace dish::net {

// Async gateway to the satellite's protocol-1 client REST API (HTTPS :9443).
// Mirrors dish-android core/net/SatelliteHttpClient.kt. Every authenticated
// route attaches `X-Device-Id` + `X-Hmac-Proof` (the proof =
// hex(HMAC-SHA256(pairingKey, "satellite-proof:" + deviceId)), computed by the
// caller via core/wire/SessionCrypto and passed in). Callbacks fire on the
// network manager's home thread (the Qt main thread).
//
// TLS: the satellite presents a self-signed cert; we keep today's VerifyNone
// behaviour (the equivalent of `curl --insecure`). TOFU cert-pinning is a later
// wave — `pinSeam_` is the clearly-marked hook where the pin verify will land
// (storage lives in a future pin repository); it is unused today so behaviour
// does not regress.
class HTTPClient : public QObject {
    Q_OBJECT
  public:
    explicit HTTPClient(QObject* parent = nullptr);
    ~HTTPClient() override;

    using SessionCb = std::function<void(const models::SessionResponse&)>;
    using ControllerCb = std::function<void(const models::ControllerPutResponse&)>;
    using ViewCb = std::function<void(const models::SessionViewDto&)>;
    using CapabilitiesCb = std::function<void(const models::CapabilitiesDto&)>;
    using CatalogCb = std::function<void(const models::CatalogDto&)>;
    // Generic ack for routes the caller doesn't decode (DELETE pair/session):
    // the HTTP status + whether a body parsed (for the terminal-401 check).
    using AckCb = std::function<void(int httpStatus, bool reachable, const QString& code)>;

    // PUT /api/connections — declarative session upsert. `controllers` is the
    // WHOLE desired set; `mouseControl` is the requested host feature (v1 sends
    // false — the touchpad-mouse UI is deferred — but still parses the grant).
    void putSession(const QString& ip, int port, const QString& deviceId, const QString& deviceName,
                    const QString& hmacProof,
                    const QList<models::ControllerDescriptor>& controllers, bool mouseControl,
                    SessionCb cb);

    // GET /api/connections/{id} — the reconcile endpoint (applied state + epoch).
    void getSession(const QString& ip, int port, const QString& connectionId,
                    const QString& deviceId, const QString& hmacProof, ViewCb cb);

    // DELETE /api/connections/{id} — graceful close (no notify).
    void deleteSession(const QString& ip, int port, const QString& connectionId,
                       const QString& deviceId, const QString& hmacProof, AckCb cb);

    // PUT /api/connections/{id}/controllers/{idx} — standalone single-descriptor
    // upsert; converges without rotating the token (path idx wins).
    void putController(const QString& ip, int port, const QString& connectionId,
                       const QString& deviceId, const QString& hmacProof,
                       const models::ControllerDescriptor& descriptor, ControllerCb cb);

    // DELETE /api/connections/{id}/controllers/{idx} — removes the SLOT only.
    void deleteController(const QString& ip, int port, const QString& connectionId, int ctrlIdx,
                          const QString& deviceId, const QString& hmacProof, ControllerCb cb);

    // DELETE /api/pair — client self-unpair (X-Device-Id + X-Hmac-Proof).
    void unpair(const QString& ip, int port, const QString& deviceId, const QString& hmacProof,
                AckCb cb);

    // GET /api/server/capabilities — unauthenticated DYNAMIC backend health.
    void getCapabilities(const QString& ip, int port, CapabilitiesCb cb);

    // GET /api/catalog — unauthenticated localized catalog. `acceptLanguage` is
    // the locale chain; `etag` (if non-empty) rides If-None-Match → 304 (the
    // caller serves its cache). The reply carries the response ETag for caching.
    void getCatalog(const QString& ip, int port, const QString& acceptLanguage, const QString& etag,
                    CatalogCb cb);

  private:
    // One exchange's decoded result, before route-specific DTO parsing.
    struct RawReply {
        int status = 0;         // 0 = transport never produced a response
        bool reachable = false; // a body (any) was received
        QByteArray body;
        QString etag;
    };

    // Issue `method` with the protocol-1 headers; invoke `done` with the raw
    // result on the home thread. `body` empty = no request body.
    void perform(const QString& url, const QByteArray& method, const QByteArray& body,
                 const QString& deviceId, const QString& hmacProof, const QString& acceptLanguage,
                 const QString& ifNoneMatch, std::function<void(const RawReply&)> done);

    QNetworkAccessManager* nam_;
};

} // namespace dish::net
