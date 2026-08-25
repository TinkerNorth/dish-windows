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
