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

TEST_CASE("the connect token is unsigned, above INT32_MAX as well as below",
          "[moonlight][rtsp][connectdata]") {
    auto tokenOf = [](const std::string& value) {
        const auto resp = parseRtspResponse("RTSP/1.0 200 OK\r\nCSeq: 5\r\n"
                                            "Transport: server_port=47999\r\n"
                                            "X-SS-Connect-Data: " +
                                            value + "\r\n\r\n");
        REQUIRE(resp.has_value());
        return setupConnectData(*resp);
    };

    // Below INT32_MAX, where a signed parse also happens to work.
    REQUIRE(*tokenOf("1182733900") == 1182733900u);
    REQUIRE(*tokenOf("2147483647") == 2147483647u); // INT32_MAX itself
    REQUIRE(*tokenOf("0") == 0u);

    // Above it, where a signed parse silently yields nothing and the control
    // stream connects with a token of zero. 4270471497 came off a live Sunshine
    // host; a signed 32-bit read of it is -24495799.
    REQUIRE(*tokenOf("2147483648") == 2147483648u);
    REQUIRE(*tokenOf("4270471497") == 4270471497u);
    REQUIRE(*tokenOf("4294967295") == 4294967295u); // UINT32_MAX

    // Whitespace around the value is tolerated; an absent or empty header is
    // not invented.
    REQUIRE(*tokenOf("  4270471497 ") == 4270471497u);
    REQUIRE_FALSE(tokenOf("").has_value());
    const auto none = parseRtspResponse("RTSP/1.0 200 OK\r\nCSeq: 5\r\n\r\n");
    REQUIRE_FALSE(setupConnectData(*none).has_value());
}

TEST_CASE("the media ping payload is taken verbatim, not decoded", "[moonlight][rtsp]") {
    const auto resp = parseRtspResponse("RTSP/1.0 200 OK\r\n"
                                        "CSeq: 3\r\n"
                                        "Session: DEADBEEFCAFE;timeout = 90\r\n"
                                        "Transport: server_port=48000\r\n"
                                        "X-SS-Ping-Payload: 988E4FC7E070A22F\r\n"
                                        "\r\n");
    REQUIRE(resp.has_value());
    REQUIRE(setupPingPayload(*resp) == "988E4FC7E070A22F");
    REQUIRE(setupPingPayload(*resp).size() == 16);

    const auto without = parseRtspResponse("RTSP/1.0 200 OK\r\nCSeq: 3\r\n\r\n");
    REQUIRE(setupPingPayload(*without).empty());
}

TEST_CASE("one message per connection: a Content-length frames the reply", "[moonlight][rtsp]") {
    const std::string headers = "RTSP/1.0 200 OK\r\nCSeq: 2\r\nContent-length: 10\r\n\r\n";
    // Header block alone is not a whole message while a body is promised.
    REQUIRE_FALSE(rtspResponseComplete(headers));
    REQUIRE(*rtspContentLength(headers) == 10);
    REQUIRE_FALSE(rtspResponseComplete(headers + "12345"));
    REQUIRE(rtspResponseComplete(headers + "1234567890"));
    // Anything past the declared length still counts as complete; the reader
    // stops asking for more.
    REQUIRE(rtspResponseComplete(headers + "1234567890extra"));

    // A partial header block is never complete.
    REQUIRE_FALSE(rtspResponseComplete("RTSP/1.0 200 OK\r\nCSeq: 2\r\n"));
    REQUIRE_FALSE(rtspResponseComplete(""));

    // The header name is matched case-insensitively; hosts spell it both ways.
    REQUIRE(*rtspContentLength("RTSP/1.0 200 OK\r\nContent-Length: 7\r\n\r\n") == 7);
    REQUIRE(*rtspContentLength("RTSP/1.0 200 OK\ncontent-length:  7 \n\n") == 7);
    // A Content-length inside the BODY must not frame the message.
    REQUIRE_FALSE(rtspContentLength("RTSP/1.0 200 OK\r\n\r\nContent-length: 99\r\n").has_value());
}

TEST_CASE("a reply with no Content-length is framed by the close", "[moonlight][rtsp]") {
    // This is the DESCRIBE answer: a host sends the SDP with no length header at
    // all and simply hangs up, so the rest of the stream is the body. It never
    // reads as complete, which is what tells the reader to keep going to EOF.
    const std::string describe = "RTSP/1.0 200 OK\r\n"
                                 "CSeq: 2\r\n"
                                 "\r\n"
                                 "a=x-ss-general.featureFlags:3\r\n"
                                 "sprop-parameter-sets=AAAAAU\r\n";
    REQUIRE_FALSE(rtspContentLength(describe).has_value());
    REQUIRE_FALSE(rtspResponseComplete(describe));
    // Once the socket is closed the caller parses what it has, and everything
    // after the blank line is the payload.
    const auto resp = parseRtspResponse(describe);
    REQUIRE(resp.has_value());
    REQUIRE(resp->statusCode == 200);
    REQUIRE(resp->payloads.size() == 2);
    REQUIRE(resp->payloads[0].first == "a");
    REQUIRE(resp->payloads[1].first == "sprop-parameter-sets");

    // A header-only reply with no body and no length is the same shape: the
    // OPTIONS and PLAY answers a host closes on.
    REQUIRE_FALSE(rtspResponseComplete("RTSP/1.0 200 OK\r\nCSeq: 1\r\n\r\n"));
    REQUIRE(parseRtspResponse("RTSP/1.0 200 OK\r\nCSeq: 1\r\n\r\n")->statusCode == 200);
}

TEST_CASE("the ANNOUNCE SDP carries the whole attribute set", "[moonlight][rtsp][sdp]") {
    const std::string sdp = buildAnnounceSdp(1280, 720, 30);

    // A minimal SDP is answered 400 BAD REQUEST; a host looks each of these up
    // by name. Every attribute the working handshake carried is asserted here so
    // one cannot be dropped as unused.
    for (const char* attribute : {"v=0",
                                  "o=android 0 14 IN IPv4 0.0.0.0",
                                  "s=NVIDIA Streaming Client",
                                  "a=x-nv-video[0].clientViewportWd:1280",
                                  "a=x-nv-video[0].clientViewportHt:720",
                                  "a=x-nv-video[0].maxFPS:30",
                                  "a=x-nv-video[0].packetSize:1024",
                                  "a=x-nv-video[0].rateControlMode:4",
                                  "a=x-nv-video[0].timeoutLengthMs:7000",
                                  "a=x-nv-video[0].framesWithInvalidRefThreshold:0",
                                  "a=x-nv-video[0].refPicInvalidation:0",
                                  "a=x-nv-video[0].encoderCscMode:0",
                                  "a=x-nv-video[0].dynamicRangeMode:0",
                                  "a=x-nv-video[0].maxNumReferenceFrames:1",
                                  "a=x-nv-video[0].videoEncoderSlicesPerFrame:1",
                                  "a=x-nv-video[0].clientRefreshRateX100:3000",
                                  "a=x-nv-vqos[0].bitStreamFormat:0",
                                  "a=x-nv-vqos[0].bw.minimumBitrateKbps:500",
                                  "a=x-nv-vqos[0].bw.maximumBitrateKbps:500",
                                  "a=x-nv-vqos[0].fec.enable:1",
                                  "a=x-nv-vqos[0].fec.minRequiredFecPackets:2",
                                  "a=x-nv-vqos[0].fec.repairPercent:20",
                                  "a=x-nv-vqos[0].drc.enable:0",
                                  "a=x-nv-vqos[0].videoQualityScoreUpdateTime:5000",
                                  "a=x-nv-vqos[0].qosTrafficType:5",
                                  "a=x-nv-aqos.qosTrafficType:4",
                                  "a=x-nv-aqos.packetDuration:5",
                                  "a=x-nv-audio.surround.numChannels:2",
                                  "a=x-nv-audio.surround.channelMask:3",
                                  "a=x-nv-audio.surround.enable:0",
                                  "a=x-nv-audio.surround.AudioQuality:0",
                                  "a=x-nv-general.useReliableUdp:13",
                                  "a=x-nv-general.featureFlags:167",
                                  "a=x-ml-general.featureFlags:3",
                                  "a=x-ss-general.encryptionEnabled:0",
                                  "t=0 0"}) {
        INFO(attribute);
        REQUIRE(sdp.find(std::string(attribute) + "\r\n") != std::string::npos);
    }

    // 36 CRLF-terminated lines, nothing dangling.
    std::size_t lines = 0;
    for (std::size_t at = sdp.find("\r\n"); at != std::string::npos;
         at = sdp.find("\r\n", at + 2)) {
        ++lines;
    }
    REQUIRE(lines == 36);
    REQUIRE(sdp.substr(sdp.size() - 2) == "\r\n");
    // The exact byte count a live Sunshine host logged as this ANNOUNCE's
    // Content-Length, so a silently reworded attribute cannot pass.
    REQUIRE(sdp.size() == 1246);

    // The mode is the caller's, not a constant: viewport and refresh follow it.
    const std::string uhd = buildAnnounceSdp(3840, 2160, 60);
    REQUIRE(uhd.find("a=x-nv-video[0].clientViewportWd:3840\r\n") != std::string::npos);
    REQUIRE(uhd.find("a=x-nv-video[0].clientViewportHt:2160\r\n") != std::string::npos);
    REQUIRE(uhd.find("a=x-nv-video[0].maxFPS:60\r\n") != std::string::npos);
    REQUIRE(uhd.find("a=x-nv-video[0].clientRefreshRateX100:6000\r\n") != std::string::npos);

    // And it rides an ANNOUNCE with the Content-length the host frames it by.
    const std::string request =
        buildRtspRequest("ANNOUNCE", "rtsp://10.0.0.5:48010", 6,
                         {{"Content-type", "application/sdp"}, {"Session", "DEADBEEFCAFE"}}, sdp);
    REQUIRE(request.find("Content-length: " + std::to_string(sdp.size()) + "\r\n") !=
            std::string::npos);
    REQUIRE(request.find("\r\n\r\nv=0\r\n") != std::string::npos);
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
