// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight HTTP(S) control API client. Two transports:
//   * HTTP  :47989 (plaintext)  - /serverinfo and pairing phases 1-4
//   * HTTPS :47984 (client cert) - phase 5, /applist, /launch, /resume, /cancel
//
// The host presents a self-signed certificate, so peer verification is disabled
// and trust is Trust-On-First-Use pinning (same doctrine as the Satellite path,
// source/http/SatelliteTlsVerifier). Every HTTPS call presents the client's
// Moonlight identity certificate, which is how a paired host authorises it.
//
// Responses are the small Moonlight XML documents; a minimal tag reader pulls
// out the fields we need rather than pulling in a full XML dependency.

#pragma once

#include "core/moonlight/MoonlightIdentity.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <optional>

class QNetworkAccessManager;

namespace dish::net {

// A parsed Moonlight XML response: the flat leaf-tag -> text map plus the
// root status. `reachable` is false when the transport produced no answer.
struct MoonlightXmlResponse {
    bool reachable = false;
    int statusCode = 0; // root status_code attribute, 0 if absent
    std::map<QString, QString> values;
    // The raw document, for list-shaped responses (/applist) whose repeated
    // nodes the flat map cannot carry.
    QByteArray rawBody;

    QString value(const QString& tag) const {
        const auto it = values.find(tag);
        return it == values.end() ? QString() : it->second;
    }
    bool paired() const { return value(QStringLiteral("paired")) == QLatin1String("1"); }
};

class MoonlightHttpClient : public QObject {
    Q_OBJECT
  public:
    explicit MoonlightHttpClient(QObject* parent = nullptr);
    ~MoonlightHttpClient() override;

    // Present this identity on every HTTPS request. Set before any HTTPS call.
    void setClientIdentity(const moonlight::Identity& identity) { identity_ = identity; }

    // TOFU pin gate. Returning false aborts the TLS handshake. `certDer` is the
    // peer cert DER. Mirrors HTTPClient::setPinVerifier.
    using PinVerifier = std::function<bool(const QString& host, const QByteArray& certDer)>;
    void setPinVerifier(PinVerifier v) { pinVerifier_ = std::move(v); }

    using ResponseCb = std::function<void(const MoonlightXmlResponse&)>;

    // GET over plaintext HTTP :httpPort. `query` is appended as ?k=v&... .
    void getHttp(const QString& host, int httpPort, const QString& path,
                 const std::map<QString, QString>& query, ResponseCb cb);

    // GET over HTTPS :httpsPort presenting the client certificate.
    void getHttps(const QString& host, int httpsPort, const QString& path,
                  const std::map<QString, QString>& query, ResponseCb cb);

  private:
    void perform(const QString& url, bool https, ResponseCb cb);

    QNetworkAccessManager* nam_;
    std::optional<moonlight::Identity> identity_;
    PinVerifier pinVerifier_;
};

// Parse a Moonlight XML document (flat, one level of leaf tags under <root>).
// Exposed for unit testing without a live server.
MoonlightXmlResponse parseMoonlightXml(const QByteArray& body);

// One /applist entry.
struct MoonlightApp {
    QString id;
    QString title;
};

// Parse the /applist document's repeated <App><ID/><AppTitle/></App> nodes,
// which the flat map above would collapse. Malformed entries are skipped.
QList<MoonlightApp> parseMoonlightAppList(const QByteArray& body);

// Build a "key=value&..." query string, percent-encoding values.
QString buildMoonlightQuery(const std::map<QString, QString>& query);

} // namespace dish::net
