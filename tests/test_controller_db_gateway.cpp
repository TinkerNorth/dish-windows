// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What a controller database request carries (docs/controller-db.md section
// 6.8, PRIVACY.md section 2.5): the request the gateway builds, sent by a real
// QNetworkAccessManager to a listener on loopback that records the bytes, so
// the headers Qt adds on its own are seen too.

#include "source/controllerdb/ControllerDbGateway.h"

#include "RecordingHttpServer.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using dish::source::controllerdb::controllerDbRequest;
using dish::source::controllerdb::kDbAcceptLanguage;
using dish::test::RecordingHttpServer;

namespace {

QUrl manifestUrlOf(const RecordingHttpServer& server) {
    return server.url(QStringLiteral("controllers/v1/manifest.json"));
}

// Sends the request and waits for the listener to have it.
void sendThrough(RecordingHttpServer& server, const QNetworkRequest& request) {
    QNetworkAccessManager nam;
    nam.setCookieJar(nullptr);
    const std::unique_ptr<QNetworkReply> reply(nam.get(request));
    REQUIRE(dish::test::spinUntilReady([&server] { return server.answered(); }));
    REQUIRE(dish::test::spinUntilReady([&reply] { return reply->isFinished(); }));
}

} // namespace

TEST_CASE("controller db request: Accept-Language is fixed, never the system's languages",
          "[controllerdb][gateway]") {
    RecordingHttpServer server;
    REQUIRE(server.listening());
    sendThrough(server, controllerDbRequest(manifestUrlOf(server), QByteArray()));
    CHECK(server.linesNamed("Accept-Language") ==
          std::vector<std::string>{std::string("Accept-Language: ") + kDbAcceptLanguage});
}

TEST_CASE("controller db request: If-None-Match only when there is an ETag",
          "[controllerdb][gateway]") {
    SECTION("with one") {
        RecordingHttpServer server;
        REQUIRE(server.listening());
        sendThrough(server,
                    controllerDbRequest(manifestUrlOf(server), QByteArrayLiteral("\"v1\"")));
        CHECK(server.linesNamed("If-None-Match") ==
              std::vector<std::string>{"If-None-Match: \"v1\""});
    }
    SECTION("without") {
        RecordingHttpServer server;
        REQUIRE(server.listening());
        sendThrough(server, controllerDbRequest(manifestUrlOf(server), QByteArray()));
        CHECK(server.linesNamed("If-None-Match").empty());
    }
}

// Pins every header the request carries, so a new one cannot arrive unnoticed: the policy names
// each of them (PRIVACY.md section 2.5).
TEST_CASE("controller db request: it carries these headers and no others",
          "[controllerdb][gateway]") {
    RecordingHttpServer server;
    REQUIRE(server.listening());
    sendThrough(server, controllerDbRequest(manifestUrlOf(server), QByteArray()));
    std::vector<std::string> names;
    for (const QByteArray& line : server.headerLines()) {
        names.push_back(line.left(line.indexOf(':')).toLower().toStdString());
    }
    std::sort(names.begin(), names.end());
    // Cache-Control and Pragma are Qt's for a request that must not be answered from a cache.
    CHECK(names == std::vector<std::string>{"accept-encoding", "accept-language", "cache-control",
                                            "connection", "host", "pragma", "user-agent"});
    CHECK(server.linesNamed("User-Agent") == std::vector<std::string>{"User-Agent: Mozilla/5.0"});
}
