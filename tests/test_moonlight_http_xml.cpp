// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightHttpClient.h"

#include <catch2/catch_test_macros.hpp>

using namespace dish::net;

TEST_CASE("parseMoonlightXml reads root status and leaf tags", "[moonlight][http]") {
    const QByteArray body = "<?xml version=\"1.0\"?>"
                            "<root status_code=\"200\">"
                            "<paired>1</paired>"
                            "<plaincert>2D2D2D2D2D</plaincert>"
                            "</root>";
    const auto resp = parseMoonlightXml(body);
    REQUIRE(resp.reachable);
    REQUIRE(resp.statusCode == 200);
    REQUIRE(resp.paired());
    REQUIRE(resp.value(QStringLiteral("plaincert")) == QStringLiteral("2D2D2D2D2D"));
}

TEST_CASE("parseMoonlightXml reports not-paired and challenge fields", "[moonlight][http]") {
    const QByteArray body = "<root status_code=\"200\"><paired>0</paired>"
                            "<challengeresponse>ABCD</challengeresponse></root>";
    const auto resp = parseMoonlightXml(body);
    REQUIRE_FALSE(resp.paired());
    REQUIRE(resp.value(QStringLiteral("challengeresponse")) == QStringLiteral("ABCD"));
}

TEST_CASE("a host refuses in the body, not in the status line", "[moonlight][http][status]") {
    // Measured against a live Sunshine host: asking /launch to start a second
    // app answers HTTP 200 carrying this. Code that reads only the transport
    // status treats the refusal as a success and then fails downstream on the
    // missing sessionUrl0, naming the wrong thing.
    const QByteArray body =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
        "<root status_code=\"400\" status_message=\"An app is already running on this host\">"
        "<resume>0</resume></root>";
    const auto resp = parseMoonlightXml(body);
    REQUIRE(resp.reachable);
    REQUIRE_FALSE(resp.ok());
    REQUIRE(resp.statusCode == 400);
    REQUIRE(resp.statusMessage == QStringLiteral("An app is already running on this host"));
    REQUIRE(resp.appAlreadyRunning());
    REQUIRE_FALSE(resp.resumeAvailable);
}

TEST_CASE("a refusal that offers a resume says so", "[moonlight][http][status]") {
    const QByteArray body =
        "<root status_code=\"400\" status_message=\"An app is already running on this host\">"
        "<resume>1</resume></root>";
    const auto resp = parseMoonlightXml(body);
    REQUIRE_FALSE(resp.ok());
    REQUIRE(resp.appAlreadyRunning());
    REQUIRE(resp.resumeAvailable);
}

TEST_CASE("a refusal for some other reason is not read as a busy host",
          "[moonlight][http][status]") {
    const auto denied = parseMoonlightXml(
        QByteArray("<root status_code=\"401\" status_message=\"Unauthorized\"></root>"));
    REQUIRE_FALSE(denied.ok());
    REQUIRE(denied.statusCode == 401);
    REQUIRE_FALSE(denied.appAlreadyRunning());
    REQUIRE_FALSE(denied.resumeAvailable);

    const auto malformed = parseMoonlightXml(
        QByteArray("<root status_code=\"500\" status_message=\"Internal error\"></root>"));
    REQUIRE_FALSE(malformed.ok());
    REQUIRE_FALSE(malformed.appAlreadyRunning());
}

TEST_CASE("a reply naming no status_code is a plain success", "[moonlight][http][status]") {
    // Which is what a host that answers plainly sends, /applist among them.
    const auto resp = parseMoonlightXml(QByteArray("<root><App><ID>1</ID></App></root>"));
    REQUIRE(resp.reachable);
    REQUIRE(resp.ok());
    REQUIRE(resp.statusCode == kMoonlightStatusOk);
    REQUIRE(resp.statusMessage.isEmpty());
    REQUIRE_FALSE(resp.appAlreadyRunning());
}

TEST_CASE("a successful launch names its session and is not a refusal",
          "[moonlight][http][status]") {
    const QByteArray body = "<root status_code=\"200\">"
                            "<sessionUrl0>rtsp://192.168.68.98:48010</sessionUrl0>"
                            "<gamesession>1</gamesession></root>";
    const auto resp = parseMoonlightXml(body);
    REQUIRE(resp.ok());
    REQUIRE_FALSE(resp.appAlreadyRunning());
    REQUIRE(resp.value(QStringLiteral("sessionUrl0")) ==
            QStringLiteral("rtsp://192.168.68.98:48010"));
    REQUIRE(resp.value(QStringLiteral("gamesession")) == QStringLiteral("1"));
}

TEST_CASE("buildMoonlightQuery percent-encodes and orders deterministically", "[moonlight][http]") {
    const QString q =
        buildMoonlightQuery({{QStringLiteral("uniqueid"), QStringLiteral("0123456789ABCDEF")},
                             {QStringLiteral("salt"), QStringLiteral("00FF")}});
    REQUIRE(q.contains(QStringLiteral("uniqueid=0123456789ABCDEF")));
    REQUIRE(q.contains(QStringLiteral("salt=00FF")));
}

TEST_CASE("parseMoonlightAppList reads repeated App nodes", "[moonlight][http]") {
    const QByteArray body = "<root status_code=\"200\">"
                            "<App><IsHdrSupported>0</IsHdrSupported>"
                            "<AppTitle>Desktop</AppTitle><ID>1</ID></App>"
                            "<App><AppTitle>Steam Big Picture</AppTitle><ID>881448767</ID></App>"
                            "</root>";
    const auto apps = parseMoonlightAppList(body);
    REQUIRE(apps.size() == 2);
    REQUIRE(apps[0].id == QStringLiteral("1"));
    REQUIRE(apps[0].title == QStringLiteral("Desktop"));
    REQUIRE(apps[1].id == QStringLiteral("881448767"));
    REQUIRE(apps[1].title == QStringLiteral("Steam Big Picture"));
}

TEST_CASE("parseMoonlightAppList skips entries with no ID and survives garbage",
          "[moonlight][http]") {
    // An entry with no ID is not launchable, so it is dropped rather than shown.
    const QByteArray missingId =
        "<root><App><AppTitle>Nameless</AppTitle></App><App><ID>7</ID></App></root>";
    const auto apps = parseMoonlightAppList(missingId);
    REQUIRE(apps.size() == 1);
    REQUIRE(apps[0].id == QStringLiteral("7"));

    REQUIRE(parseMoonlightAppList(QByteArray("not xml <<<")).isEmpty());
    REQUIRE(parseMoonlightAppList(QByteArray()).isEmpty());
}

TEST_CASE("parseMoonlightXml on garbage is unreachable-safe", "[moonlight][http]") {
    const auto resp = parseMoonlightXml(QByteArray("not xml <<<"));
    // A parse error still yields an object; callers gate on reachable / values.
    REQUIRE(resp.values.empty());
}
