// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightRtspClient.h"

#include "core/moonlight/MoonlightRtsp.h"

#include <QByteArray>
#include <QTcpSocket>

#include <map>

namespace dish::net {

namespace {
namespace ml = dish::moonlight;

// Send one RTSP request and read one response off `socket`. Moonlight closes the
// message with the TCP framing; we read until the response parses or the socket
// goes quiet.
std::optional<ml::RtspResponse> exchange(QTcpSocket& socket, const std::string& request,
                                         int timeoutMs) {
    const QByteArray out = QByteArray::fromStdString(request);
    if (socket.write(out) != out.size()) { return std::nullopt; }
    if (!socket.waitForBytesWritten(timeoutMs)) { return std::nullopt; }

    QByteArray buffer;
    // RTSP responses are small; read until we can parse a complete one or time
    // out. The status line + a blank line terminates the header block.
    while (socket.waitForReadyRead(timeoutMs)) {
        buffer += socket.readAll();
        const auto parsed = ml::parseRtspResponse(buffer.toStdString());
        if (parsed.has_value() && buffer.contains("\r\n\r\n")) { return parsed; }
    }
    if (!buffer.isEmpty()) { return ml::parseRtspResponse(buffer.toStdString()); }
    return std::nullopt;
}

} // namespace

std::optional<RtspHandshakeResult> MoonlightRtspClient::handshake(const std::string& host,
                                                                  std::uint16_t rtspPort, int width,
                                                                  int height, int fps,
                                                                  int timeoutMs) {
    QTcpSocket socket;
    socket.connectToHost(QString::fromStdString(host), rtspPort);
    if (!socket.waitForConnected(timeoutMs)) { return std::nullopt; }

    const std::string target = "rtsp://" + host + ":" + std::to_string(rtspPort);
    int cseq = 1;
    const std::map<std::string, std::string> baseOpts = {{"X-GS-ClientVersion", "14"}};

    // OPTIONS
    if (auto r =
            exchange(socket, ml::buildRtspRequest("OPTIONS", target, cseq++, baseOpts), timeoutMs);
        !r || r->statusCode != 200) {
        return std::nullopt;
    }

    // DESCRIBE
    if (auto r =
            exchange(socket, ml::buildRtspRequest("DESCRIBE", target, cseq++, baseOpts), timeoutMs);
        !r || r->statusCode != 200) {
        return std::nullopt;
    }

    RtspHandshakeResult result;

    // SETUP audio / video / control. Each returns its server_port.
    auto setup = [&](const char* stream) -> std::optional<ml::RtspResponse> {
        std::map<std::string, std::string> opts = baseOpts;
        opts["Transport"] = "unicast;X-GS-ClientPort=50000-50001";
        opts["Session"] = "DEADBEEFCAFE";
        const std::string setupTarget = std::string("streamid=") + stream + "/0/0";
        return exchange(socket, ml::buildRtspRequest("SETUP", setupTarget, cseq++, opts),
                        timeoutMs);
    };

    if (auto r = setup("audio"); r && r->statusCode == 200) {
        if (auto p = ml::setupServerPort(*r)) { result.audioPort = static_cast<std::uint16_t>(*p); }
        const auto ping = r->options.find("X-SS-Ping-Payload");
        if (ping != r->options.end()) { result.audioPingPayload = ping->second; }
    } else {
        return std::nullopt;
    }
    if (auto r = setup("video"); r && r->statusCode == 200) {
        if (auto p = ml::setupServerPort(*r)) { result.videoPort = static_cast<std::uint16_t>(*p); }
        const auto ping = r->options.find("X-SS-Ping-Payload");
        if (ping != r->options.end()) { result.videoPingPayload = ping->second; }
    } else {
        return std::nullopt;
    }
    if (auto r = setup("control"); r && r->statusCode == 200) {
        if (auto p = ml::setupServerPort(*r)) {
            result.controlPort = static_cast<std::uint16_t>(*p);
        }
        if (auto cd = ml::setupConnectData(*r)) { result.connectData = *cd; }
    } else {
        return std::nullopt;
    }

    // ANNOUNCE the (minimal) session config. We do not decode media, so the
    // numbers only have to be well-formed for the host to accept and PLAY.
    std::string sdp;
    sdp += "v=0\r\n";
    sdp += "a=x-nv-video[0].clientViewportWd:" + std::to_string(width) + " \r\n";
    sdp += "a=x-nv-video[0].clientViewportHt:" + std::to_string(height) + " \r\n";
    sdp += "a=x-nv-video[0].maxFPS:" + std::to_string(fps) + " \r\n";
    sdp += "a=x-nv-video[0].packetSize:1024 \r\n";
    sdp += "a=x-nv-vqos[0].bw.maximumBitrateKbps:5000 \r\n";
    sdp += "a=x-nv-vqos[0].fec.minRequiredFecPackets:2 \r\n";
    sdp += "a=x-nv-general.featureFlags:167 \r\n";
    sdp += "a=x-nv-audio.surround.numChannels:2 \r\n";
    sdp += "a=x-nv-video[0].encoderCscMode:0 \r\n";
    {
        std::map<std::string, std::string> opts = baseOpts;
        opts["Session"] = "DEADBEEFCAFE";
        opts["Content-type"] = "application/sdp";
        if (auto r = exchange(
                socket,
                ml::buildRtspRequest("ANNOUNCE", "streamid=control/13/0", cseq++, opts, sdp),
                timeoutMs);
            !r || r->statusCode != 200) {
            return std::nullopt;
        }
    }

    // PLAY.
    if (auto r =
            exchange(socket, ml::buildRtspRequest("PLAY", target, cseq++, baseOpts), timeoutMs);
        !r || r->statusCode != 200) {
        return std::nullopt;
    }

    socket.disconnectFromHost();
    return result;
}

} // namespace dish::net
