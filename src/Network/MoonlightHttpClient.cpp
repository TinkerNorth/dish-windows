// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightHttpClient.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

namespace dish::net {

MoonlightHttpClient::MoonlightHttpClient(QObject* parent)
    : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

MoonlightHttpClient::~MoonlightHttpClient() = default;

QString buildMoonlightQuery(const std::map<QString, QString>& query) {
    QUrlQuery q;
    for (const auto& [key, value] : query) { q.addQueryItem(key, value); }
    return q.toString(QUrl::FullyEncoded);
}

MoonlightXmlResponse parseMoonlightXml(const QByteArray& body) {
    MoonlightXmlResponse resp;
    QXmlStreamReader xml(body);
    QString currentTag;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("root")) {
                const auto attrs = xml.attributes();
                if (attrs.hasAttribute(QStringLiteral("status_code"))) {
                    resp.statusCode = attrs.value(QStringLiteral("status_code")).toInt();
                }
            } else {
                currentTag = xml.name().toString();
            }
        } else if (token == QXmlStreamReader::Characters && !currentTag.isEmpty()) {
            if (!xml.isWhitespace()) { resp.values[currentTag] = xml.text().toString(); }
        } else if (token == QXmlStreamReader::EndElement) {
            currentTag.clear();
        }
    }
    if (!xml.hasError()) { resp.reachable = true; }
    return resp;
}

void MoonlightHttpClient::getHttp(const QString& host, int httpPort, const QString& path,
                                  const std::map<QString, QString>& query, ResponseCb cb) {
    QString url = QStringLiteral("http://%1:%2%3").arg(host).arg(httpPort).arg(path);
    const QString qs = buildMoonlightQuery(query);
    if (!qs.isEmpty()) { url += QLatin1Char('?') + qs; }
    perform(url, /*https=*/false, std::move(cb));
}

void MoonlightHttpClient::getHttps(const QString& host, int httpsPort, const QString& path,
                                   const std::map<QString, QString>& query, ResponseCb cb) {
    QString url = QStringLiteral("https://%1:%2%3").arg(host).arg(httpsPort).arg(path);
    const QString qs = buildMoonlightQuery(query);
    if (!qs.isEmpty()) { url += QLatin1Char('?') + qs; }
    perform(url, /*https=*/true, std::move(cb));
}

void MoonlightHttpClient::perform(const QString& url, bool https, ResponseCb cb) {
    QNetworkRequest request{QUrl(url)};

    if (https) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        // Self-signed host cert: trust comes from the TOFU pin below, not a CA.
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        if (identity_.has_value()) {
            const QSslCertificate cert(QByteArray::fromStdString(identity_->certPem), QSsl::Pem);
            const QSslKey key(QByteArray::fromStdString(identity_->privateKeyPem), QSsl::Rsa,
                              QSsl::Pem);
            if (!cert.isNull()) { ssl.setLocalCertificate(cert); }
            if (!key.isNull()) { ssl.setPrivateKey(key); }
        }
        request.setSslConfiguration(ssl);
    }

    QNetworkReply* reply = nam_->get(request);

    if (https && pinVerifier_) {
        const QString host = QUrl(url).host();
        QObject::connect(reply, &QNetworkReply::encrypted, reply, [this, reply, host] {
            const auto chain = reply->sslConfiguration().peerCertificateChain();
            const QByteArray der = chain.isEmpty() ? QByteArray() : chain.first().toDer();
            if (!pinVerifier_(host, der)) { reply->abort(); }
        });
    }

    QObject::connect(reply, &QNetworkReply::finished, this, [reply, cb = std::move(cb)] {
        MoonlightXmlResponse resp;
        if (reply->error() == QNetworkReply::NoError) {
            resp = parseMoonlightXml(reply->readAll());
            resp.reachable = true;
        } else {
            resp.reachable = false;
        }
        reply->deleteLater();
        if (cb) { cb(resp); }
    });
}

} // namespace dish::net
