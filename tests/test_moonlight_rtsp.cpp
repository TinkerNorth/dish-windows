// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "core/moonlight/MoonlightRtsp.h"

#include <catch2/catch_test_macros.hpp>

using namespace dish::moonlight;

TEST_CASE("buildRtspRequest formats an OPTIONS request", "[moonlight][rtsp]") {
    const std::string req =
        buildRtspRequest("OPTIONS", "rtsp://10.0.0.5:48010", 1, {{"X-GS-ClientVersion", "14"}});
    REQUIRE(req == "OPTIONS rtsp://10.0.0.5:48010 RTSP/1.0\r\n"
                   "CSeq: 1\r\n"
                   "X-GS-ClientVersion: 14\r\n"
                   "\r\n");
}

TEST_CASE("buildRtspRequest appends a payload with Content-length", "[moonlight][rtsp]") {
    const std::string req = buildRtspRequest("ANNOUNCE", "streamid=control/13/0", 6, {},
                                             "v=0\r\na=x-nv-video[0].maxFPS:30\r\n");
    REQUIRE(req.find("ANNOUNCE streamid=control/13/0 RTSP/1.0\r\n") == 0);
    REQUIRE(req.find("CSeq: 6\r\n") != std::string::npos);
    REQUIRE(req.find("Content-length: 32\r\n") != std::string::npos);
    REQUIRE(req.find("\r\n\r\nv=0") != std::string::npos);
}

TEST_CASE("parseRtspResponse reads status, CSeq and Transport", "[moonlight][rtsp]") {
    const std::string text = "RTSP/1.0 200 OK\r\n"
                             "CSeq: 3\r\n"
                             "Session: DEADBEEFCAFE;timeout = 90\r\n"
                             "Transport: server_port=48010\r\n"
                             "\r\n";
    const auto resp = parseRtspResponse(text);
    REQUIRE(resp.has_value());
    REQUIRE(resp->statusCode == 200);
    REQUIRE(resp->cseq == 3);
    REQUIRE(resp->options.at("Session") == "DEADBEEFCAFE;timeout = 90");
    REQUIRE(setupServerPort(*resp).has_value());
    REQUIRE(*setupServerPort(*resp) == 48010);
}

TEST_CASE("parseRtspResponse reads control connect data", "[moonlight][rtsp]") {
    const std::string text = "RTSP/1.0 200 OK\r\n"
                             "CSeq: 5\r\n"
                             "Transport: server_port=47999\r\n"
                             "X-SS-Connect-Data: 305419896\r\n"
                             "\r\n";
    const auto resp = parseRtspResponse(text);
    REQUIRE(resp.has_value());
    REQUIRE(*setupServerPort(*resp) == 47999);
    REQUIRE(setupConnectData(*resp).has_value());
    REQUIRE(*setupConnectData(*resp) == 305419896u);
}

TEST_CASE("parseRtspResponse tolerates bare LF and collects payloads", "[moonlight][rtsp]") {
    const std::string text = "RTSP/1.0 200 OK\n"
                             "CSeq: 2\n"
                             "\n"
                             "sprop-parameter-sets=AAAAAU\n"
                             "a=fmtp:97 surround-params=21101\n";
    const auto resp = parseRtspResponse(text);
    REQUIRE(resp.has_value());
    REQUIRE(resp->statusCode == 200);
    REQUIRE(resp->cseq == 2);
    REQUIRE(resp->payloads.size() == 2);
    REQUIRE(resp->payloads[0].first == "sprop-parameter-sets");
    REQUIRE(resp->payloads[0].second == "AAAAAU");
}

TEST_CASE("parseRtspResponse rejects a non-response", "[moonlight][rtsp]") {
    REQUIRE_FALSE(parseRtspResponse("OPTIONS rtsp://x RTSP/1.0\r\nCSeq: 1\r\n\r\n").has_value());
    REQUIRE_FALSE(parseRtspResponse("").has_value());
}

TEST_CASE("serverPortFromTransport handles decoration and absence", "[moonlight][rtsp]") {
    REQUIRE(*serverPortFromTransport("unicast;server_port=48010;foo=bar") == 48010);
    REQUIRE_FALSE(serverPortFromTransport("unicast;client_port=50000").has_value());
    REQUIRE_FALSE(serverPortFromTransport("server_port=").has_value());
}
