// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightHttpClient.h"

#include "source/http/HttpTransport.h"

#include <QLoggingCategory>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QUrl>
#include <QUrlQuery>
#include <QXmlStreamReader>

#include <utility>

namespace dish::net {

Q_LOGGING_CATEGORY(lcMoonlightHttp, "dish.moonlight.http")

MoonlightHttpClient::MoonlightHttpClient(QObject* parent)
    : QObject(parent), transport_(new http::HttpTransport(this)) {}

MoonlightHttpClient::~MoonlightHttpClient() = default;

QString buildMoonlightQuery(const std::map<QString, QString>& query) {
    QUrlQuery q;
    for (const auto& [key, value] : query) { q.addQueryItem(key, value); }
    return q.toString(QUrl::FullyEncoded);
}

MoonlightXmlResponse parseMoonlightXml(const QByteArray& body, int httpStatus) {
    MoonlightXmlResponse resp;
    resp.statusCode = httpStatus;
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

namespace {

// Self-signed host cert, so trust comes from the TOFU pin rather than a CA. Never offer a ticket,
// never share a session between sockets, never persist one: see the header. A resumed handshake is
// the one thing a Moonlight host will not survive.
QSslConfiguration sslConfigFor(const std::optional<moonlight::Identity>& identity) {
    QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
    ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
    ssl.setSslOption(QSsl::SslOptionDisableSessionTickets, true);
    ssl.setSslOption(QSsl::SslOptionDisableSessionSharing, true);
    ssl.setSslOption(QSsl::SslOptionDisableSessionPersistence, true);
    ssl.setSessionTicket(QByteArray());
    if (identity.has_value()) {
        const QSslCertificate cert(QByteArray::fromStdString(identity->certPem), QSsl::Pem);
        const QSslKey key(QByteArray::fromStdString(identity->privateKeyPem), QSsl::Rsa, QSsl::Pem);
        if (!cert.isNull()) { ssl.setLocalCertificate(cert); }
        if (!key.isNull()) { ssl.setPrivateKey(key); }
    }
    return ssl;
}

MoonlightXmlResponse unanswered(const http::HttpResult& result, const QString& path) {
    qCWarning(lcMoonlightHttp) << path << "unreachable:" << result.error;
    return {};
}

// A host says no in the body as often as in the status line, so an answer is read whatever the
// status line says, and only the body's own status_code outranks it.
MoonlightXmlResponse answered(const http::HttpResult& result, const QString& path) {
    const int httpStatus = result.response.status;
    MoonlightXmlResponse resp =
        parseMoonlightXml(QByteArray::fromStdString(result.response.body), httpStatus);
    resp.reachable = true;
    resp.httpStatus = httpStatus;
    if (resp.ok()) {
        qCDebug(lcMoonlightHttp) << path << "answered" << resp.statusCode;
    } else {
        qCWarning(lcMoonlightHttp)
            << path << "refused:" << resp.statusCode << resp.statusMessage << "http" << httpStatus
            << "resume available:" << resp.resumeAvailable;
    }
    return resp;
}

// reachable is the distinction that matters to every caller: a host that answered, whatever it
// said, is a different thing from one that did not answer at all.
MoonlightXmlResponse readReply(const http::HttpResult& result, const QString& path) {
    const bool anythingAnswered = result.response.status != 0;
    return anythingAnswered ? answered(result, path) : unanswered(result, path);
}

void handOver(const QString& path, const MoonlightHttpClient::ResponseCb& cb,
              const http::HttpResult& result) {
    const MoonlightXmlResponse resp = readReply(result, path);
    if (cb) { cb(resp); }
}

} // namespace

// The pin is checked on `encrypted`, the last moment before any request bytes go out, so a host
// whose certificate changed never sees the request at all. The verifier is the one in force when
// the call was made: a store detaches it with calls still in flight. No call carries a deadline:
// pairing phase 1 waits on a human typing the PIN into the host.
void MoonlightHttpClient::perform(const QString& url, bool https, ResponseCb cb) {
    http::HttpRequest request;
    request.url = QUrl(url);
    request.method = QByteArrayLiteral("GET");
    request.tls = https ? std::optional(sslConfigFor(identity_)) : std::nullopt;
    http::PeerCheck peerCheck;
    if (pinVerifier_) {
        peerCheck = [verifier = pinVerifier_, host = request.url.host()](const QByteArray& der) {
            return verifier(host, der);
        };
    }
    const QString path = request.url.path();
    transport_->exchange(
        std::move(request), std::move(peerCheck),
        [path, cb = std::move(cb)](const http::HttpResult& result) { handOver(path, cb, result); });
}

} // namespace dish::net
