// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightHttpClient.h"

#include <QLoggingCategory>
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

Q_LOGGING_CATEGORY(lcMoonlightHttp, "dish.moonlight.http")

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
    resp.rawBody = body;
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
                resp.statusMessage = attrs.value(QStringLiteral("status_message")).toString();
            } else {
                currentTag = xml.name().toString();
            }
        } else if (token == QXmlStreamReader::Characters && !currentTag.isEmpty()) {
            if (!xml.isWhitespace()) { resp.values[currentTag] = xml.text().toString(); }
        } else if (token == QXmlStreamReader::EndElement) {
            currentTag.clear();
        }
    }
    resp.resumeAvailable = resp.value(QStringLiteral("resume")) == QLatin1String("1");
    if (!xml.hasError()) { resp.reachable = true; }
    return resp;
}

QList<MoonlightApp> parseMoonlightAppList(const QByteArray& body) {
    QList<MoonlightApp> apps;
    QXmlStreamReader xml(body);
    MoonlightApp current;
    bool inApp = false;
    QString leaf;
    while (!xml.atEnd()) {
        const auto token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            if (xml.name() == QLatin1String("App")) {
                inApp = true;
                current = MoonlightApp{};
            } else if (inApp) {
                leaf = xml.name().toString();
            }
        } else if (token == QXmlStreamReader::Characters && inApp && !leaf.isEmpty() &&
                   !xml.isWhitespace()) {
            if (leaf == QLatin1String("ID")) {
                current.id = xml.text().toString();
            } else if (leaf == QLatin1String("AppTitle")) {
                current.title = xml.text().toString();
            }
        } else if (token == QXmlStreamReader::EndElement) {
            if (xml.name() == QLatin1String("App")) {
                inApp = false;
                if (!current.id.isEmpty()) { apps.append(current); }
            }
            leaf.clear();
        }
    }
    return apps;
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
    // Sunshine speaks HTTP/1.1 only, and one plain request per connection is
    // what the teardown below can reason about.
    request.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    if (https) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        // Self-signed host cert: trust comes from the TOFU pin below, not a CA.
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        // Never offer a ticket, never share a session between sockets, never
        // persist one: see the header. A resumed handshake is the one thing a
        // Moonlight host will not survive.
        ssl.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
        ssl.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
        ssl.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
        ssl.setSessionTicket(QByteArray());
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

    const QString path = QUrl(url).path();
    QObject::connect(
        reply, &QNetworkReply::finished, this, [this, reply, path, cb = std::move(cb)] {
            MoonlightXmlResponse resp;
            if (reply->error() == QNetworkReply::NoError) {
                resp = parseMoonlightXml(reply->readAll());
                resp.reachable = true;
                if (resp.ok()) {
                    qCDebug(lcMoonlightHttp) << path << "answered" << resp.statusCode;
                } else {
                    qCWarning(lcMoonlightHttp)
                        << path << "refused in the body:" << resp.statusCode << resp.statusMessage
                        << "resume available:" << resp.resumeAvailable;
                }
            } else {
                resp.reachable = false;
                qCWarning(lcMoonlightHttp) << path << "unreachable:" << reply->errorString();
            }
            reply->deleteLater();
            // Leave the host holding nothing of ours between calls: a pooled idle
            // connection is a socket the host has to keep, and the next request
            // through it would be the one offering a session to resume.
            nam_->clearConnectionCache();
            if (cb) { cb(resp); }
        });
}

} // namespace dish::net
