// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// MoonlightHttpClient's TLS calls against a real listener on loopback: the pin is checked before
// the request is written, the identity a host demands is presented, a host's answer is read
// whatever its HTTP status says, and a call waits as long as the host takes. The listener is all a
// GameStream host's HTTPS port is from here: something that speaks TLS and records what reached it.
// The probe cases read that answer one level up, where a session decides whether a host is there.

#include "Network/MoonlightHost.h"
#include "Network/MoonlightHttpClient.h"
#include "Network/MoonlightSession.h"
#include "core/moonlight/MoonlightIdentity.h"

#include "FakeHttpsListener.h"
#include "MoonlightFakeHost.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QHostAddress>
#include <QSslCertificate>
#include <QString>
#include <QTcpServer>
#include <QTcpSocket>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using dish::net::MoonlightHttpClient;
using dish::net::MoonlightSession;
using dish::net::MoonlightXmlResponse;
using dish::test::FakeHttpsListener;
using dish::test::HttpsAnswer;
using dish::test::SeenRequest;

namespace {

const QString kLoopback = QStringLiteral("127.0.0.1");

// A plaintext origin that answers every connection with one bare status line: what a host still
// starting up, or another service on the GameStream port, sends a probe.
class BareStatusOrigin {
  public:
    explicit BareStatusOrigin(QByteArray statusLine) : statusLine_(std::move(statusLine)) {
        listening_ = server_.listen(QHostAddress::LocalHost, 0);
        QObject::connect(&server_, &QTcpServer::newConnection, &server_, [this] { answerAll(); });
    }

    bool listening() const { return listening_; }
    int port() const { return static_cast<int>(server_.serverPort()); }

  private:
    void answerAll() {
        while (QTcpSocket* sock = server_.nextPendingConnection()) {
            sock->write(statusLine_ + "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
            sock->disconnectFromHost();
        }
    }

    QTcpServer server_;
    QByteArray statusLine_;
    bool listening_ = false;
};

// Probes the host whose plaintext port is `httpPort` and answers whether the session found it.
std::optional<bool> probeAnswered(int httpPort) {
    dish::models::MoonlightHost host;
    host.name = QStringLiteral("Den");
    host.ip = kLoopback;
    host.httpPort = httpPort;
    host.httpsPort = dish::test::closedLoopbackPort();
    const auto identity = dish::moonlight::generateIdentity();
    REQUIRE(identity.has_value());
    MoonlightSession session(host, *identity, nullptr);
    std::optional<bool> answered;
    QObject::connect(&session, &MoonlightSession::probeFinished, &session,
                     [&answered](bool hostAnswered, const QString&) { answered = hostAnswered; });
    session.probe();
    dish::test::spinUntil([&answered] { return answered.has_value(); });
    return answered;
}

// A client carrying an identity of its own, as a paired install does.
struct PairedClient {
    dish::moonlight::Identity identity;
    MoonlightHttpClient http;

    PairedClient()
        : identity(dish::moonlight::generateIdentity().value_or(dish::moonlight::Identity{})) {
        http.setClientIdentity(identity);
    }
};

// What a pin verifier was shown.
struct VerifierLog {
    std::vector<QByteArray> certs;
};

// One TLS /serverinfo, turned until it lands or `timeoutMs` passes.
std::optional<MoonlightXmlResponse> serverInfoFrom(MoonlightHttpClient& http, int port,
                                                   int timeoutMs = 20000) {
    std::optional<MoonlightXmlResponse> got;
    http.getHttps(kLoopback, port, QStringLiteral("/serverinfo"),
                  {{QStringLiteral("uniqueid"), QStringLiteral("0123456789ABCDEF")}},
                  [&got](const MoonlightXmlResponse& r) { got = r; });
    dish::test::spinUntil([&got] { return got.has_value(); }, timeoutMs);
    return got;
}

QByteArray derOf(const std::string& pem) {
    return QSslCertificate(QByteArray::fromStdString(pem), QSsl::Pem).toDer();
}

} // namespace

TEST_CASE("moonlight http: a host the pin refuses is sent nothing", "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    PairedClient client;
    const auto log = std::make_shared<VerifierLog>();
    client.http.setPinVerifier([log](const QString&, const QByteArray& der) {
        log->certs.push_back(der);
        return false;
    });

    const auto got = serverInfoFrom(client.http, host.port());
    dish::test::spinUntil([] { return false; }, 200);

    REQUIRE(got.has_value());
    CHECK_FALSE(got->reachable);
    // Shown the certificate the host presented, and then not the request, nor the rikey a launch
    // carries in it.
    REQUIRE(log->certs.size() == 1);
    CHECK(log->certs.front() == host.certDer());
    CHECK(host.bytesReceived() == 0);
    CHECK(host.requests().empty());
}

TEST_CASE("moonlight http: a verifier withdrawn mid-call still judges the call it was made for",
          "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    PairedClient client;
    const auto log = std::make_shared<VerifierLog>();
    client.http.setPinVerifier([log](const QString&, const QByteArray& der) {
        log->certs.push_back(der);
        return true;
    });
    std::optional<MoonlightXmlResponse> got;
    client.http.getHttps(kLoopback, host.port(), QStringLiteral("/cancel"), {},
                         [&got](const MoonlightXmlResponse& r) { got = r; });

    // What forgetting a host does to its session, with a TLS call already out.
    client.http.setPinVerifier(nullptr);

    REQUIRE(dish::test::spinUntil([&got] { return got.has_value(); }));
    CHECK(got->reachable);
    CHECK(log->certs.size() == 1);
}

TEST_CASE("moonlight http: a TLS call presents the identity the host demands",
          "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    host.askForClientCertificates();
    PairedClient client;

    const auto got = serverInfoFrom(client.http, host.port());

    REQUIRE(got.has_value());
    CHECK(got->reachable);
    REQUIRE(host.requests().size() == 1);
    // What a host's verify callback holds up against the device it paired.
    CHECK(host.requests().front().clientCertificate.toDer() == derOf(client.identity.certPem));
}

TEST_CASE("moonlight http: a refusal sent with an HTTP error status is read, not taken for silence",
          "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    // Wolf's answer to a launch of an app it does not have: HTTP 400, its reason in the body.
    host.respond = [](const SeenRequest&) {
        return HttpsAnswer{400, R"(<root status_code="400" status_message="App not found"/>)"};
    };
    PairedClient client;

    const auto got = serverInfoFrom(client.http, host.port());

    REQUIRE(got.has_value());
    CHECK(got->reachable);
    CHECK(got->httpStatus == 400);
    CHECK_FALSE(got->ok());
    CHECK(got->statusMessage == QStringLiteral("App not found"));
}

TEST_CASE("moonlight http: an HTTP error with no reason in its body is still a refusal",
          "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    host.respond = [](const SeenRequest&) { return HttpsAnswer{404, "asset not found"}; };
    PairedClient client;

    const auto got = serverInfoFrom(client.http, host.port());

    REQUIRE(got.has_value());
    CHECK(got->reachable);
    CHECK(got->httpStatus == 404);
    CHECK_FALSE(got->ok());
}

TEST_CASE("moonlight http: a 401 from the host is trust lost", "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    // What Wolf answers a client it does not know over TLS.
    host.respond = [](const SeenRequest&) {
        return HttpsAnswer{401, R"(<root status_code="401" status_message="The client is not )"
                                R"(authorized. Certificate verification failed."/>)"};
    };
    PairedClient client;

    const auto got = serverInfoFrom(client.http, host.port());

    REQUIRE(got.has_value());
    CHECK(got->reachable);
    CHECK(got->unauthorized());
    CHECK_FALSE(got->ok());
}

TEST_CASE("moonlight http: a call waits as long as the host takes", "[moonlight][http][wire]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener host;
    REQUIRE(host.listening());
    host.respond = [](const SeenRequest&) {
        return HttpsAnswer{200, R"(<root status_code="200"><paired>1</paired></root>)"};
    };
    host.hold();
    PairedClient client;
    std::optional<MoonlightXmlResponse> got;
    client.http.getHttps(kLoopback, host.port(), QStringLiteral("/pair"), {},
                         [&got](const MoonlightXmlResponse& r) { got = r; });

    // Pairing phase 1 waits on a human typing the PIN into the host; nothing here cuts it short.
    REQUIRE(dish::test::spinUntil([&host] { return host.requests().size() == 1; }));
    dish::test::spinUntil([&got] { return got.has_value(); }, 1500);
    CHECK_FALSE(got.has_value());

    host.release();
    REQUIRE(dish::test::spinUntil([&got] { return got.has_value(); }));
    CHECK(got->reachable);
    CHECK(got->ok());
}

TEST_CASE("moonlight http: a probe answered by serverinfo has found the host",
          "[moonlight][http][wire]") {
    dish::test::ensureApp();
    dish::test::MoonlightFakeHost fake(QStringLiteral("4271"));
    REQUIRE(fake.listening());

    const auto answered = probeAnswered(fake.port());

    REQUIRE(answered.has_value());
    CHECK(*answered);
}

TEST_CASE("moonlight http: a probe answered with an HTTP error has not found the host",
          "[moonlight][http][wire]") {
    dish::test::ensureApp();
    BareStatusOrigin origin("HTTP/1.1 503 Service Unavailable");
    REQUIRE(origin.listening());

    const auto answered = probeAnswered(origin.port());

    REQUIRE(answered.has_value());
    CHECK_FALSE(*answered);
}
