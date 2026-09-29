// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A satellite's REST API answered in-process. It is a QNetworkAccessManager, so
// the WifiConnectionManager under test makes its real HTTPClient calls, with
// their real URLs, headers and JSON decoding, and nothing reaches a socket.
// Every request is recorded; each is answered from the script a test sets up,
// on a later event-loop turn the way a real reply lands. A one-shot answer is
// used before the route's standing one; a route with neither is answered the
// way a dead transport answers: no status, no body. Between hold() and
// release(), replies wait: the window a test needs to act while a reply is
// still on its way.

#pragma once

#include <QByteArray>
#include <QHash>
#include <QIODevice>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>
#include <QVariant>

#include <algorithm>
#include <cstring>
#include <vector>

namespace dish::test {

struct CannedAnswer {
    int status = 0; // 0 = the transport never produced a response
    QByteArray body;
};

struct RecordedRequest {
    QByteArray verb;
    QString host;
    QString path;
    QUrlQuery query;
    QByteArray deviceId;
    QByteArray hmacProof;
    QByteArray body;
};

class CannedReply : public QNetworkReply {
  public:
    // `delivered` is counted up as the reply lands; it belongs to the manager
    // that parents this reply, so it outlives it. With `handshake`, the reply
    // first passes the TLS edge its client's pin verifier runs on.
    CannedReply(const QNetworkRequest& request, QNetworkAccessManager::Operation op,
                const CannedAnswer& answer, bool handshake, long long* delivered, QObject* parent)
        : QNetworkReply(parent), body_(answer.body), handshake_(handshake), delivered_(delivered) {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        if (answer.status != 0) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, answer.status);
        }
        open(QIODevice::ReadOnly);
    }

    void deliverLater() { QTimer::singleShot(0, this, &CannedReply::deliver); }

    // What a real reply does when its client hangs up: it ends at once, with no
    // status and no body.
    void abort() override {
        ++*delivered_;
        body_.clear();
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, QVariant());
        setError(OperationCanceledError, QStringLiteral("Operation canceled"));
        setFinished(true);
        emit errorOccurred(OperationCanceledError);
        emit finished();
    }
    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override {
        const qint64 unread = body_.size() - offset_;
        return unread + QIODevice::bytesAvailable();
    }

  protected:
    qint64 readData(char* data, qint64 maxSize) override {
        const qint64 unread = body_.size() - offset_;
        const qint64 count = std::min(maxSize, unread);
        std::memcpy(data, body_.constData() + offset_, static_cast<std::size_t>(count));
        offset_ += count;
        return count;
    }

  private:
    void deliver() {
        if (handshake_) { emit encrypted(); }
        // A verifier that refused on the TLS edge has already aborted the reply.
        if (isFinished()) { return; }
        ++*delivered_;
        setFinished(true);
        emit metaDataChanged();
        emit readyRead();
        emit finished();
    }

    QByteArray body_;
    qint64 offset_ = 0;
    bool handshake_ = false;
    long long* delivered_;
};

class FakeSatelliteRest : public QNetworkAccessManager {
  public:
    void answer(const QByteArray& verb, const QString& path, const CannedAnswer& reply) {
        script_.insert(routeKey(verb, path), reply);
    }

    void answerOnce(const QByteArray& verb, const QString& path, const CannedAnswer& reply) {
        onceScript_[routeKey(verb, path)].push_back(reply);
    }

    // From here on, every reply passes the TLS edge first, where its client's pin verifier runs.
    void handshakeEveryReply() { handshaking_ = true; }

    void hold() { holding_ = true; }
    void release() {
        holding_ = false;
        for (const auto& reply : held_) {
            if (!reply.isNull()) { reply->deliverLater(); }
        }
        held_.clear();
    }

    const std::vector<RecordedRequest>& requests() const { return requests_; }

    // True once every request made so far has had its reply delivered.
    bool allAnswered() const { return delivered_ == static_cast<long long>(requests_.size()); }

    long long count(const QByteArray& verb, const QString& path) const {
        return std::count_if(requests_.begin(), requests_.end(), [&](const RecordedRequest& r) {
            return r.verb == verb && r.path == path;
        });
    }

  protected:
    QNetworkReply* createRequest(Operation op, const QNetworkRequest& request,
                                 QIODevice* outgoingData) override {
        RecordedRequest recorded;
        recorded.verb = verbOf(op, request);
        recorded.host = request.url().host();
        recorded.path = request.url().path();
        recorded.query = QUrlQuery(request.url());
        recorded.deviceId = request.rawHeader("X-Device-Id");
        recorded.hmacProof = request.rawHeader("X-Hmac-Proof");
        recorded.body = outgoingData != nullptr ? outgoingData->readAll() : QByteArray();
        requests_.push_back(recorded);
        const CannedAnswer answer = nextAnswer(routeKey(recorded.verb, recorded.path));
        auto* reply = new CannedReply(request, op, answer, handshaking_, &delivered_, this);
        if (holding_) {
            held_.emplace_back(reply);
        } else {
            reply->deliverLater();
        }
        return reply;
    }

  private:
    CannedAnswer nextAnswer(const QByteArray& key) {
        auto& once = onceScript_[key];
        if (once.empty()) { return script_.value(key); }
        const CannedAnswer reply = once.front();
        once.erase(once.begin());
        return reply;
    }

    static QByteArray routeKey(const QByteArray& verb, const QString& path) {
        return verb + ' ' + path.toUtf8();
    }

    static QByteArray verbOf(Operation op, const QNetworkRequest& request) {
        switch (op) {
        case HeadOperation:
            return "HEAD";
        case GetOperation:
            return "GET";
        case PutOperation:
            return "PUT";
        case PostOperation:
            return "POST";
        case DeleteOperation:
            return "DELETE";
        case CustomOperation:
            return request.attribute(QNetworkRequest::CustomVerbAttribute).toByteArray();
        case UnknownOperation:
            break;
        }
        return {};
    }

    QHash<QByteArray, CannedAnswer> script_;
    QHash<QByteArray, std::vector<CannedAnswer>> onceScript_;
    std::vector<RecordedRequest> requests_;
    long long delivered_ = 0;
    bool handshaking_ = false;
    bool holding_ = false;
    std::vector<QPointer<CannedReply>> held_;
};

} // namespace dish::test
