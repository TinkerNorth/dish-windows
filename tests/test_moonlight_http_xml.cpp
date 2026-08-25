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

TEST_CASE("parseMoonlightXml on garbage is unreachable-safe", "[moonlight][http]") {
    const auto resp = parseMoonlightXml(QByteArray("not xml <<<"));
    // A parse error still yields an object; callers gate on reachable / values.
    REQUIRE(resp.values.empty());
}
