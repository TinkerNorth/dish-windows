// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A satellite's REST API answered in-process. It is an HttpTransport, so the
// WifiConnectionManager under test makes its real HTTPClient calls, with their
// real URLs, headers and JSON decoding, and nothing reaches a socket. Every
// request is recorded; each is answered from the script a test sets up, on a
// later event-loop turn the way a real reply lands. A one-shot answer is used
// before the route's standing one; a route with neither is answered the way a
// dead transport answers: no status, no body. Between hold() and release(),
// replies wait: the window a test needs to act while a reply is still on its
// way.

#pragma once

#include "source/http/HttpTransport.h"

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <string_view>
#include <utility>
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

// The value of the header field `name` a request carries, or empty.
inline QByteArray headerOf(const dish::http::HttpRequest& request, std::string_view name) {
    const auto field = std::find_if(
        request.headers.cbegin(), request.headers.cend(),
        [name](const dish::wire::HttpHeader& candidate) { return candidate.first == name; });
    return field == request.headers.cend() ? QByteArray()
                                           : QByteArray::fromStdString(field->second);
}

class CannedReply : public QObject {
  public:
    // `delivered` is counted up as the reply lands; it belongs to the transport
    // that parents this reply, so it outlives it. With `handshake`, the reply
    // first passes the TLS edge its client's pin check runs on, shown the no
    // certificate an in-process reply has.
    CannedReply(const CannedAnswer& answer, bool handshake, dish::http::PeerCheck peerCheck,
                dish::http::HttpDone done, long long* delivered, QObject* parent)
        : QObject(parent), answer_(answer), handshake_(handshake), peerCheck_(std::move(peerCheck)),
          done_(std::move(done)), delivered_(delivered) {}

    void deliverLater() { QTimer::singleShot(0, this, &CannedReply::deliver); }

  private:
    void deliver() {
        ++*delivered_;
        // What a real reply does when its client hangs up on the TLS edge: it
        // ends at once, with no status and no body.
        const bool refused = handshake_ && peerCheck_ && !peerCheck_(QByteArray());
        dish::http::HttpResult result;
        if (!refused) {
            result.response.status = answer_.status;
            result.response.body = answer_.body.toStdString();
        }
        const dish::http::HttpDone done = std::move(done_);
        deleteLater();
        done(result);
    }

    CannedAnswer answer_;
    bool handshake_ = false;
    dish::http::PeerCheck peerCheck_;
    dish::http::HttpDone done_;
    long long* delivered_;
};

class FakeSatelliteRest : public dish::http::HttpTransport {
  public:
    void answer(const QByteArray& verb, const QString& path, const CannedAnswer& reply) {
        script_.insert(routeKey(verb, path), reply);
    }

    void answerOnce(const QByteArray& verb, const QString& path, const CannedAnswer& reply) {
        onceScript_[routeKey(verb, path)].push_back(reply);
    }

    // From here on, every reply passes the TLS edge first, where its client's pin check runs.
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

    void exchange(dish::http::HttpRequest request, dish::http::PeerCheck peerCheck,
                  dish::http::HttpDone done) override {
        RecordedRequest recorded;
        recorded.verb = request.method;
        recorded.host = request.url.host();
        recorded.path = request.url.path();
        recorded.query = QUrlQuery(request.url);
        recorded.deviceId = headerOf(request, "X-Device-Id");
        recorded.hmacProof = headerOf(request, "X-Hmac-Proof");
        recorded.body = request.body;
        requests_.push_back(recorded);
        const CannedAnswer answer = nextAnswer(routeKey(recorded.verb, recorded.path));
        auto* reply = new CannedReply(answer, handshaking_, std::move(peerCheck), std::move(done),
                                      &delivered_, this);
        if (holding_) {
            held_.emplace_back(reply);
        } else {
            reply->deliverLater();
        }
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

    QHash<QByteArray, CannedAnswer> script_;
    QHash<QByteArray, std::vector<CannedAnswer>> onceScript_;
    std::vector<RecordedRequest> requests_;
    long long delivered_ = 0;
    bool handshaking_ = false;
    bool holding_ = false;
    std::vector<QPointer<CannedReply>> held_;
};

} // namespace dish::test
