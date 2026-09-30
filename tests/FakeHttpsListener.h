// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// An HTTPS endpoint on loopback, reduced to what a client can observe: a satellite's REST port or
// a GameStream host's TLS port. It records every request, headers and all, with every byte any
// connection delivered and the certificate the client presented, and answers through a responder
// the test sets, or with exactly the bytes a test wants on the wire. TLS is real, under a
// self-signed identity of its own, so a client's pinned-certificate check runs against a real
// handshake.
//
// It lives on the test's own thread, beside the client under test, on the TLS backend the tests
// run on (useTestTlsBackend).

#pragma once

#include "InstalledCatalog.h"

#include "core/moonlight/MoonlightIdentity.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QHash>
#include <QHostAddress>
#include <QList>
#include <QPointer>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <QSslKey>
#include <QSslServer>
#include <QSslSocket>
#include <QString>
#include <QTcpServer>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace dish::test {

// The TLS backend these tests run on, for every TLS object this process makes from now on: OpenSSL,
// from the libssl the build keeps in build/test-tls, which ctest puts on the test's PATH (a run by
// hand needs it there too; tests/CMakeLists.txt says why it is not beside the test binary). Not
// the Schannel the app ships with, because Qt 6.7.3's debug build asserts inside its own
// certificate date parser on Schannel (QDateTimeParser, "maximum.date().toJulianDay() ==
// 5373484"), and a debug abort on Windows waits on a dialog. Nothing under test depends on the
// backend: every ordering the exchange relies on is its own. It has to run before anything in the
// process touches TLS, and after the application exists, because Qt loads its TLS backends
// through an application static.
inline bool useTestTlsBackend() {
    ensureApp();
    const bool selected = QSslSocket::setActiveBackend(QStringLiteral("openssl"));
    return selected || QSslSocket::activeBackend() == QLatin1String("openssl");
}

// Turns the event loop until `ready` holds or `timeoutMs` passes, and answers whether it held.
inline bool spinUntil(const std::function<bool()>& ready, int timeoutMs = 20000) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!ready() && !deadline.hasExpired()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return ready();
}

// A loopback port with nothing listening on it.
inline int closedLoopbackPort() {
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0)) { return 0; }
    const int port = static_cast<int>(probe.serverPort());
    probe.close();
    return port;
}

// One request as the listener received it.
struct SeenRequest {
    QByteArray method;
    QString path;
    QUrlQuery query;
    QByteArray body;
    // Every header field, keyed by its name in lower case.
    QHash<QByteArray, QByteArray> headers;
    // What the client presented, once the listener asks for one; null until then.
    QSslCertificate clientCertificate;
};

// What the listener says back, framed by its length.
struct HttpsAnswer {
    int status = 200;
    QByteArray body;
};

class FakeHttpsListener : public QObject {
  public:
    // Every request is answered by `respond`; by default a 200 with an empty JSON object.
    std::function<HttpsAnswer(const SeenRequest&)> respond = [](const SeenRequest&) {
        return HttpsAnswer{200, QByteArrayLiteral("{}")};
    };

    // When set, every request is answered with exactly these bytes instead: how a test puts
    // chunked framing, an ETag or a broken answer on the wire.
    std::function<QByteArray(const SeenRequest&)> respondRaw;

    // When positive, every answer goes out as two writes a moment apart, cut after this many
    // bytes: an answer whose head arrives in pieces.
    int splitAt = 0;

    FakeHttpsListener() {
        ensureApp();
        const auto identity = dish::moonlight::generateIdentity();
        if (!identity) { return; }
        certPem_ = QString::fromStdString(identity->certPem);
        const QSslCertificate cert(QByteArray::fromStdString(identity->certPem), QSsl::Pem);
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setLocalCertificate(cert);
        ssl.setPrivateKey(
            QSslKey(QByteArray::fromStdString(identity->privateKeyPem), QSsl::Rsa, QSsl::Pem));
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        server_.setSslConfiguration(ssl);
        certDer_ = cert.toDer();
        QObject::connect(&server_, &QTcpServer::pendingConnectionAvailable, this,
                         &FakeHttpsListener::acceptAll);
        QObject::connect(&server_, &QSslServer::sslErrors, this,
                         &FakeHttpsListener::forgiveClientCertificate);
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
    }

    bool listening() const { return listening_; }
    int port() const { return static_cast<int>(server_.serverPort()); }
    // What a client pins: the certificate this listener presents.
    QString certPem() const { return certPem_; }
    QByteArray certDer() const { return certDer_; }

    const std::vector<SeenRequest>& requests() const { return requests_; }

    // Every byte any connection delivered, whether or not it made a whole request: what "nothing
    // was written" is measured by.
    qsizetype bytesReceived() const { return bytesReceived_; }

    // From now on the handshake demands the client's certificate, and each request records it.
    // Verifying the peer, and forgiving its self-signed chain in forgiveClientCertificate, is
    // what shows a server the certificate on either backend; Schannel ignores a mere query.
    void askForClientCertificates() {
        QSslConfiguration ssl = server_.sslConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyPeer);
        server_.setSslConfiguration(ssl);
    }

    // From hold() until release(), requests are recorded but their answers wait: the window a
    // test needs to act while a reply is still on its way.
    void hold() { holding_ = true; }
    void release() {
        holding_ = false;
        for (const auto& [sock, answer] : held_) {
            if (!sock.isNull()) { write(sock.data(), answer); }
        }
        held_.clear();
    }

  private:
    // Long enough that the client has read the first part on its own before the rest exists.
    static constexpr int kSplitPauseMs = 100;

    void acceptAll() {
        while (auto* sock = qobject_cast<QSslSocket*>(server_.nextPendingConnection())) {
            auto pending = std::make_shared<QByteArray>();
            QObject::connect(sock, &QSslSocket::readyRead, sock,
                             [this, sock, pending] { onBytes(sock, *pending); });
            QObject::connect(sock, &QSslSocket::disconnected, sock, &QObject::deleteLater);
        }
    }

    static void forgiveClientCertificate(QSslSocket* sock, const QList<QSslError>&) {
        sock->ignoreSslErrors();
    }

    // Answers once the whole request, headers and Content-Length body, has arrived.
    void onBytes(QSslSocket* sock, QByteArray& pending) {
        const QByteArray fresh = sock->readAll();
        bytesReceived_ += fresh.size();
        pending.append(fresh);
        std::optional<SeenRequest> seenRequest = wholeRequest(pending);
        if (!seenRequest) { return; }
        seenRequest->clientCertificate = sock->peerCertificate();
        requests_.push_back(*seenRequest);

        const QByteArray answer =
            respondRaw ? respondRaw(*seenRequest) : encoded(respond(*seenRequest));
        if (holding_) {
            held_.emplace_back(QPointer<QSslSocket>(sock), answer);
            return;
        }
        write(sock, answer);
    }

    // The request `pending` holds, once its head and Content-Length body have both arrived.
    static std::optional<SeenRequest> wholeRequest(const QByteArray& pending) {
        const qsizetype headerEnd = pending.indexOf("\r\n\r\n");
        if (headerEnd < 0) { return std::nullopt; }
        const QByteArray head = pending.left(headerEnd);
        const QHash<QByteArray, QByteArray> headers = headersOf(head);
        const qsizetype bodyStart = headerEnd + 4;
        const qsizetype bodyLength = headers.value("content-length").toLongLong();
        if (pending.size() - bodyStart < bodyLength) { return std::nullopt; }

        const QList<QByteArray> requestLine = head.split('\n').first().trimmed().split(' ');
        const QUrl target(QString::fromLatin1(requestLine.value(1)));
        SeenRequest seenRequest;
        seenRequest.method = requestLine.value(0);
        seenRequest.path = target.path();
        seenRequest.query = QUrlQuery(target);
        seenRequest.body = pending.mid(bodyStart, bodyLength);
        seenRequest.headers = headers;
        return seenRequest;
    }

    // Every field line after the request line, keyed by its name in lower case.
    static QHash<QByteArray, QByteArray> headersOf(const QByteArray& head) {
        QHash<QByteArray, QByteArray> headers;
        const QList<QByteArray> lines = head.split('\n');
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            const qsizetype colon = line.indexOf(':');
            headers.insert(line.left(colon).trimmed().toLower(), line.mid(colon + 1).trimmed());
        }
        return headers;
    }

    // An answer framed by its length, with the connection closed after it.
    static QByteArray encoded(const HttpsAnswer& answer) {
        return "HTTP/1.1 " + QByteArray::number(answer.status) + " X\r\n" +
               "Content-Length: " + QByteArray::number(answer.body.size()) + "\r\n" +
               "Connection: close\r\n\r\n" + answer.body;
    }

    void write(QSslSocket* sock, const QByteArray& answer) {
        const bool inTwoWrites = splitAt > 0 && splitAt < answer.size();
        if (!inTwoWrites) {
            writeAndClose(sock, answer);
            return;
        }
        sock->write(answer.left(splitAt));
        sock->flush();
        const QByteArray rest = answer.mid(splitAt);
        QTimer::singleShot(kSplitPauseMs, sock, [sock, rest] { writeAndClose(sock, rest); });
    }

    static void writeAndClose(QSslSocket* sock, const QByteArray& bytes) {
        sock->write(bytes);
        sock->disconnectFromHost();
    }

    QSslServer server_;
    QString certPem_;
    QByteArray certDer_;
    bool listening_ = false;
    bool holding_ = false;
    qsizetype bytesReceived_ = 0;
    std::vector<SeenRequest> requests_;
    std::vector<std::pair<QPointer<QSslSocket>, QByteArray>> held_;
};

} // namespace dish::test
