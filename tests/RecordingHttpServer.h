// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A plain-HTTP listener on loopback that keeps the first request's head and answers it with the
// body it was given, so a test sees every header a real QNetworkAccessManager puts on a request,
// the ones Qt adds on its own included.

#pragma once

#include "InstalledCatalog.h"

#include <QByteArray>
#include <QByteArrayList>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QEventLoop>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrl>

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace dish::test {

// Runs the event loop until `ready` holds or `timeoutMs` passes; whether it held.
inline bool spinUntilReady(const std::function<bool()>& ready, int timeoutMs = 10000) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!ready()) {
        if (deadline.hasExpired()) { return false; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

class RecordingHttpServer {
  public:
    // The application first: a socket made before it has no event loop to be read on.
    explicit RecordingHttpServer(QByteArray body = QByteArray()) : body_(std::move(body)) {
        ensureApp();
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, &server_,
                         [this] { onConnection(); });
    }

    bool listening() const { return listening_; }
    bool answered() const { return answered_; }
    QUrl url(const QString& path) const {
        return QUrl(QStringLiteral("http://127.0.0.1:%1/%2").arg(server_.serverPort()).arg(path));
    }

    // The request's header lines, without the request line.
    QByteArrayList headerLines() const {
        const QByteArray head = head_.left(head_.indexOf("\r\n\r\n"));
        const QByteArrayList lines = head.split('\n');
        QByteArrayList out;
        for (qsizetype i = 1; i < lines.size(); ++i) {
            const QByteArray line = lines[i].trimmed();
            if (!line.isEmpty()) { out.append(line); }
        }
        return out;
    }

    // Text rather than bytes, so a failure prints the lines readably.
    std::vector<std::string> linesNamed(const QByteArray& name) const {
        std::vector<std::string> out;
        const QByteArray prefix = name.toLower() + ':';
        for (const QByteArray& line : headerLines()) {
            if (line.toLower().startsWith(prefix)) { out.push_back(line.toStdString()); }
        }
        return out;
    }

  private:
    void onConnection() {
        socket_ = server_.nextPendingConnection();
        QObject::connect(socket_, &QTcpSocket::readyRead, socket_, [this] { onReadyRead(); });
    }

    void onReadyRead() {
        head_.append(socket_->readAll());
        if (answered_ || !head_.contains("\r\n\r\n")) { return; }
        answered_ = true;
        const QByteArray reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
                                 "Content-Length: " +
                                 QByteArray::number(body_.size()) +
                                 "\r\nConnection: close\r\n\r\n" + body_;
        socket_->write(reply);
        socket_->disconnectFromHost();
    }

    QTcpServer server_;
    QTcpSocket* socket_ = nullptr;
    QByteArray head_;
    QByteArray body_;
    bool listening_ = false;
    bool answered_ = false;
};

} // namespace dish::test
