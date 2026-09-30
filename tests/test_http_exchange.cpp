// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What exchangeHttp promises about when its outcome arrives and what the code receiving it may do,
// against a real TLS listener on loopback. Every client leans on all three: an outcome delivered
// from inside the call re-enters a caller that has not finished starting it, one delivered after
// its owner has gone runs against an object that no longer exists, and an exchange that touches
// itself after delivering cannot let the receiver tear its owner down.
//
// The last case is a use-after-free when the exchange does touch itself afterwards, which the
// debug heap turns into a crash here and AddressSanitizer reports wherever it runs.

#include "source/http/HttpExchange.h"

#include "FakeHttpsListener.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QObject>
#include <QSslConfiguration>
#include <QSslSocket>
#include <QString>
#include <QUrl>

#include <memory>

using dish::http::exchangeHttp;
using dish::http::HttpDone;
using dish::http::HttpRequest;
using dish::http::HttpResult;
using dish::test::FakeHttpsListener;
using dish::test::spinUntil;

namespace {

// A GET from loopback `port`, over TLS or plain TCP, with no deadline of its own.
HttpRequest getFrom(int port, bool tls) {
    HttpRequest request;
    request.url = QUrl(QStringLiteral("%1://127.0.0.1:%2/v1/capabilities")
                           .arg(tls ? QStringLiteral("https") : QStringLiteral("http"))
                           .arg(port));
    request.method = QByteArrayLiteral("GET");
    if (tls) {
        QSslConfiguration ssl = QSslConfiguration::defaultConfiguration();
        ssl.setPeerVerifyMode(QSslSocket::VerifyNone);
        request.tls = ssl;
    }
    return request;
}

// Every outcome delivered, and the status of the last.
struct Delivered {
    int count = 0;
    int status = -1;
};

HttpDone countingInto(Delivered& delivered) {
    return [&delivered](const HttpResult& result) {
        ++delivered.count;
        delivered.status = result.response.status;
    };
}

// Lets whatever is still queued land, for the assertions that nothing more happened.
void settle(int ms = 300) {
    spinUntil([] { return false; }, ms);
}

} // namespace

TEST_CASE("exchange: the outcome never arrives inside the call that asked for it",
          "[http][exchange]") {
    REQUIRE(dish::test::useTestTlsBackend());
    // Port 0 is refused on the spot, so this failure is known before exchangeHttp returns: the
    // one outcome that could be handed over from inside it.
    QObject owner;
    Delivered delivered;

    exchangeHttp(&owner, getFrom(0, /*tls=*/false), {}, countingInto(delivered));
    CHECK(delivered.count == 0);

    REQUIRE(spinUntil([&delivered] { return delivered.count > 0; }));
    settle();
    CHECK(delivered.count == 1);
    CHECK(delivered.status == 0);
}

TEST_CASE("exchange: an owner destroyed first takes the outcome with it", "[http][exchange]") {
    REQUIRE(dish::test::useTestTlsBackend());
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    listener.hold();
    auto owner = std::make_unique<QObject>();
    Delivered delivered;

    exchangeHttp(owner.get(), getFrom(listener.port(), /*tls=*/true), {}, countingInto(delivered));
    REQUIRE(spinUntil([&listener] { return !listener.requests().empty(); }));
    owner.reset();
    listener.release();
    settle(500);

    CHECK(delivered.count == 0);
}

TEST_CASE("exchange: the outcome may destroy the owner it is delivered to", "[http][exchange]") {
    REQUIRE(dish::test::useTestTlsBackend());
    // A client whose last wait this answer ends tears itself down right here, with the exchange
    // still on the stack.
    FakeHttpsListener listener;
    REQUIRE(listener.listening());
    auto owner = std::make_unique<QObject>();
    Delivered delivered;

    exchangeHttp(owner.get(), getFrom(listener.port(), /*tls=*/true), {},
                 [&delivered, &owner](const HttpResult& result) {
                     ++delivered.count;
                     delivered.status = result.response.status;
                     owner.reset();
                 });
    REQUIRE(spinUntil([&delivered] { return delivered.count > 0; }));
    settle();

    CHECK(delivered.count == 1);
    CHECK(delivered.status == 200);
    CHECK(owner == nullptr);
}
