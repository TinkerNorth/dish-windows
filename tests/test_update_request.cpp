// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What the update check's request carries (PRIVACY.md section 2.4): the manifest gateway's own
// request, sent to a listener on loopback that records the bytes, so the headers Qt adds on its
// own are seen too.

#include "update/HttpGateways.h"

#include "RecordingHttpServer.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

using dish::test::RecordingHttpServer;
using dish::update::HttpManifestGateway;
using dish::update::ManifestFetchResult;

namespace {

// Runs one check against the listener and waits for its answer.
void checkAgainst(RecordingHttpServer& server) {
    HttpManifestGateway gateway;
    gateway.setUrl(server.url(QStringLiteral("latest.json")).toString());
    std::optional<ManifestFetchResult> result;
    gateway.fetch([&result](const ManifestFetchResult& r) { result = r; });
    REQUIRE(dish::test::spinUntilReady([&result] { return result.has_value(); }));
}

} // namespace

TEST_CASE("update request: Accept-Language is fixed, never the system's languages",
          "[update][http]") {
    RecordingHttpServer server(QByteArrayLiteral("{}"));
    REQUIRE(server.listening());
    checkAgainst(server);
    CHECK(server.linesNamed("Accept-Language") == std::vector<std::string>{"Accept-Language: *"});
}

// Pins every header the check carries, so a new one cannot arrive unnoticed: the policy names each
// of them (PRIVACY.md section 2.4).
TEST_CASE("update request: it carries these headers and no others", "[update][http]") {
    RecordingHttpServer server(QByteArrayLiteral("{}"));
    REQUIRE(server.listening());
    checkAgainst(server);
    std::vector<std::string> names;
    for (const QByteArray& line : server.headerLines()) {
        names.push_back(line.left(line.indexOf(':')).toLower().toStdString());
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"accept", "accept-encoding", "accept-language",
                                            "cache-control", "connection", "host", "pragma",
                                            "user-agent"});
    CHECK(server.linesNamed("Accept") == std::vector<std::string>{"Accept: application/json"});
}
