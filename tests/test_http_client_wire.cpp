// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// HTTPClient against a real TLS listener on loopback: which headers reach the satellite, every
// framing its answer can arrive in, and each way of getting no answer at all. test_pairing_wire.cpp
// and test_wifi_connection_manager.cpp answer in-process to pin what the calls mean; this pins the
// transport under every one of them.

#include "Models/Models.h"
#include "Network/HTTPClient.h"
#include "core/model/Protocol.h"

#include "FakeHttpsListener.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>
#include <optional>

using dish::models::CatalogDto;
using dish::models::SessionResponse;
using dish::net::HTTPClient;
using dish::test::FakeHttpsListener;
using dish::test::HttpsAnswer;
using dish::test::SeenRequest;

namespace {

const QString kLoopback = QStringLiteral("127.0.0.1");

// The session a satellite grants, as the JSON it sends.
const QByteArray kGrant = R"({"connectionId":"c-1","epoch":3,"token":"0a0b0c0d"})";

bool trustingVerifier(const QString&, const QByteArray&, bool&) { return true; }

// A verifier that refuses the certificate as a changed one.
bool mismatchingVerifier(const QString&, const QByteArray&, bool& pinMismatch) {
    pinMismatch = true;
    return false;
}

struct SessionReply {
    SessionResponse response;
    bool pinMismatch = false;
};

// A session PUT from dev-1, turned until it lands.
SessionReply putSessionTo(HTTPClient& client, int port) {
    std::optional<SessionReply> got;
    client.putSession(kLoopback, port, QStringLiteral("dev-1"), QStringLiteral("Den PC"),
                      QStringLiteral("0a1b2c"), {}, false, dish::proto::kProtocolVersion,
                      [&got](const SessionResponse& response, bool pinMismatch) {
                          got = SessionReply{response, pinMismatch};
                      });
    REQUIRE(dish::test::spinUntil([&got] { return got.has_value(); }));
    return *got;
}

// The catalog in German, revalidating `etag`, turned until it lands.
CatalogDto catalogFrom(HTTPClient& client, int port, const QString& etag) {
    std::optional<CatalogDto> got;
    client.getCatalog(kLoopback, port, QStringLiteral("de-DE"), etag,
                      [&got](const CatalogDto& catalog) { got = catalog; });
    REQUIRE(dish::test::spinUntil([&got] { return got.has_value(); }));
    return *got;
}

// Every request answered with the same bytes.
std::function<QByteArray(const SeenRequest&)> answering(const QByteArray& bytes) {
    return [bytes](const SeenRequest&) { return bytes; };
}

// A listener whose every answer is the session grant.
struct GrantingListener : FakeHttpsListener {
    GrantingListener() {
        respond = [](const SeenRequest&) { return HttpsAnswer{200, kGrant}; };
    }
};

// A port that answers a TLS hello with plain text, the way an HTTP server does when it is dialled
// on the wrong port.
class PlainTextPort {
  public:
    PlainTextPort() {
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] { answer(); });
    }

    bool listening() const { return listening_; }
    int port() const { return static_cast<int>(server_.serverPort()); }

  private:
    void answer() {
        while (QTcpSocket* sock = server_.nextPendingConnection()) {
            sock->write("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n");
        }
    }

    QTcpServer server_;
    bool listening_ = false;
};

} // namespace

TEST_CASE("HTTPClient wire: an authenticated call carries its device, its proof and its body",
          "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    GrantingListener listener;
    REQUIRE(listener.listening());
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    putSessionTo(client, listener.port());

    REQUIRE(listener.requests().size() == 1);
    const SeenRequest& seen = listener.requests().front();
    CHECK(seen.method == "PUT");
    CHECK(seen.headers.value("host") == "127.0.0.1:" + QByteArray::number(listener.port()));
    CHECK(seen.headers.value("connection") == "close");
    CHECK(seen.headers.value("content-type") == "application/json");
    CHECK(seen.headers.value("x-device-id") == "dev-1");
    CHECK(seen.headers.value("x-hmac-proof") == "0a1b2c");
    CHECK(seen.headers.value("content-length").toLongLong() == seen.body.size());
    const QJsonObject body = QJsonDocument::fromJson(seen.body).object();
    CHECK(body.value(QStringLiteral("deviceId")).toString() == QStringLiteral("dev-1"));
    // Nothing to say in either, so neither is said.
    CHECK_FALSE(seen.headers.contains("accept-language"));
    CHECK_FALSE(seen.headers.contains("if-none-match"));
}

TEST_CASE("HTTPClient wire: the catalog asks in the user's language and revalidates its tag",
          "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    HTTPClient client;

    catalogFrom(client, listener.port(), QStringLiteral("\"v6\""));

    REQUIRE(listener.requests().size() == 1);
    const SeenRequest& seen = listener.requests().front();
    CHECK(seen.method == "GET");
    CHECK(seen.path == QStringLiteral("/api/catalog"));
    CHECK(seen.headers.value("accept-language") == "de-DE");
    CHECK(seen.headers.value("if-none-match") == "\"v6\"");
    // Unauthenticated, and with no body to measure.
    CHECK_FALSE(seen.headers.contains("x-device-id"));
    CHECK_FALSE(seen.headers.contains("x-hmac-proof"));
    CHECK_FALSE(seen.headers.contains("content-length"));
}

TEST_CASE("HTTPClient wire: a catalog comes back with its tag", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    const QByteArray catalog = R"({"locale":"de","catalogVersion":2,"serverVersion":"2.1.0"})";
    listener.respondRaw = answering("HTTP/1.1 200 OK\r\nETag: \"v7\"\r\nContent-Length: " +
                                    QByteArray::number(catalog.size()) + "\r\n\r\n" + catalog);
    HTTPClient client;

    const CatalogDto got = catalogFrom(client, listener.port(), QString());

    CHECK(got.reachable);
    CHECK(got.httpStatus == 200);
    CHECK_FALSE(got.notModified);
    CHECK(got.etag == QStringLiteral("\"v7\""));
    CHECK(got.locale == QStringLiteral("de"));
    CHECK(got.catalogVersion == 2);
}

TEST_CASE("HTTPClient wire: a 304 keeps its tag and says the cache is good", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    listener.respondRaw = answering("HTTP/1.1 304 Not Modified\r\nETag: \"v7\"\r\n\r\n");
    HTTPClient client;

    const CatalogDto got = catalogFrom(client, listener.port(), QStringLiteral("\"v7\""));

    CHECK(got.reachable);
    CHECK(got.httpStatus == 304);
    CHECK(got.notModified);
    CHECK(got.etag == QStringLiteral("\"v7\""));
}

TEST_CASE("HTTPClient wire: a chunked answer is read whole", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    const qsizetype half = kGrant.size() / 2;
    listener.respondRaw = answering("HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n" +
                                    QByteArray::number(half, 16) + "\r\n" + kGrant.left(half) +
                                    "\r\n" + QByteArray::number(kGrant.size() - half, 16) + "\r\n" +
                                    kGrant.mid(half) + "\r\n0\r\n\r\n");
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());

    CHECK(got.response.httpStatus == 200);
    CHECK(got.response.connectionId == QStringLiteral("c-1"));
    CHECK(got.response.epoch == 3);
}

TEST_CASE("HTTPClient wire: an answer whose head arrives in two pieces is read whole",
          "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    GrantingListener listener;
    REQUIRE(listener.listening());
    // Inside "Content-Length", well before the empty line that ends the head.
    listener.splitAt = 21;
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());

    CHECK(got.response.httpStatus == 200);
    CHECK(got.response.connectionId == QStringLiteral("c-1"));
}

TEST_CASE("HTTPClient wire: an answer the satellite ends by hanging up is read whole",
          "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    listener.respondRaw =
        answering("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n\r\n" + kGrant);
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());

    CHECK(got.response.httpStatus == 200);
    CHECK(got.response.connectionId == QStringLiteral("c-1"));
}

TEST_CASE("HTTPClient wire: an answer cut short is no answer", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    listener.respondRaw = answering("HTTP/1.1 200 OK\r\nContent-Length: 50\r\n\r\n{\"connec");
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());

    CHECK_FALSE(got.response.reachable);
    CHECK(got.response.httpStatus == 0);
}

TEST_CASE("HTTPClient wire: a satellite that never answers is given up on", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    GrantingListener listener;
    REQUIRE(listener.listening());
    listener.hold();
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());

    CHECK(listener.requests().size() == 1);
    CHECK_FALSE(got.response.reachable);
    CHECK(got.response.httpStatus == 0);
}

TEST_CASE("HTTPClient wire: a port with nothing on it is unreachable", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, dish::test::closedLoopbackPort());

    CHECK_FALSE(got.response.reachable);
    CHECK_FALSE(got.pinMismatch);
}

TEST_CASE("HTTPClient wire: a port that does not speak TLS is unreachable", "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    PlainTextPort plain;
    REQUIRE(plain.listening());
    HTTPClient client;
    client.setPinVerifier(trustingVerifier);

    const SessionReply got = putSessionTo(client, plain.port());

    CHECK_FALSE(got.response.reachable);
    CHECK_FALSE(got.pinMismatch);
}

TEST_CASE("HTTPClient wire: a changed certificate hangs up before a byte is written",
          "[http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    GrantingListener listener;
    REQUIRE(listener.listening());
    HTTPClient client;
    client.setPinVerifier(mismatchingVerifier);

    const SessionReply got = putSessionTo(client, listener.port());
    dish::test::spinUntil([] { return false; }, 200);

    // Not the request, not a line of it: the proof never leaves this machine.
    CHECK(listener.bytesReceived() == 0);
    CHECK(listener.requests().empty());
    CHECK(got.pinMismatch);
    CHECK_FALSE(got.response.reachable);
}
