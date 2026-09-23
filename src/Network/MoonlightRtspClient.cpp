// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightRtspClient.h"

#include "core/moonlight/MoonlightRtsp.h"

#include <QByteArray>
#include <QLoggingCategory>
#include <QString>
#include <QTcpSocket>

#include <utility>

namespace dish::net {

Q_LOGGING_CATEGORY(lcMoonlightRtsp, "dish.moonlight.rtsp")

namespace {
namespace ml = dish::moonlight;

// Built per call rather than held as a file-static: a std::pair of std::string with static storage
// duration allocates during static initialization, where a throw has nowhere to go.
//
// GFE's protocol version, as every Moonlight client sends it. A host that sees no version assumes a
// much older client and answers a handshake this one cannot finish.
std::pair<std::string, std::string> clientVersionHeader() { return {"X-GS-ClientVersion", "14"}; }

// The session id is the client's to choose and the host only echoes it, so it is a constant rather
// than anything derived: this is the value the reference clients send.
std::pair<std::string, std::string> sessionHeader() { return {"Session", "DEADBEEFCAFE"}; }

// Line ends spelled out, so a framing bug is readable in a log line.
QString escaped(const QByteArray& raw) {
    constexpr int kRawLogChars = 512;
    return QString::fromUtf8(raw.left(kRawLogChars))
        .replace(QLatin1String("\r"), QLatin1String("\\r"))
        .replace(QLatin1String("\n"), QLatin1String("\\n"));
}

} // namespace

MoonlightRtspClient::MoonlightRtspClient(std::string host, std::uint16_t rtspPort, int timeoutMs)
    : host_(std::move(host)), rtspPort_(rtspPort), timeoutMs_(timeoutMs) {}

std::optional<ml::RtspResponse>
MoonlightRtspClient::send(const std::string& command, const std::string& target,
                          const std::map<std::string, std::string>& options,
                          const std::string& payload) {
    const int cseq = nextCseq();
    stage_ = command + " (CSeq " + std::to_string(cseq) + ")";

    QTcpSocket socket;
    socket.connectToHost(QString::fromStdString(host_), rtspPort_);
    if (!socket.waitForConnected(timeoutMs_)) {
        qCWarning(lcMoonlightRtsp)
            << "connect for" << QString::fromStdString(stage_) << "failed:" << socket.errorString();
        return std::nullopt;
    }

    const QByteArray out =
        QByteArray::fromStdString(ml::buildRtspRequest(command, target, cseq, options, payload));
    qCDebug(lcMoonlightRtsp) << "->" << QString::fromStdString(stage_) << "target"
                             << QString::fromStdString(target) << out.size() << "bytes";
    if (socket.write(out) != out.size() || !socket.waitForBytesWritten(timeoutMs_)) {
        qCWarning(lcMoonlightRtsp)
            << QString::fromStdString(stage_) << "could not be written:" << socket.errorString();
        return std::nullopt;
    }

    // Content-length frames the reply when the host sends one. It does not on
    // DESCRIBE, and since it closes the connection once it has answered, the
    // rest of the stream is the body.
    QByteArray buffer;
    for (;;) {
        buffer += socket.readAll();
        if (ml::rtspResponseComplete(buffer.toStdString())) { break; }
        if (socket.state() == QAbstractSocket::UnconnectedState) { break; }
        if (!socket.waitForReadyRead(timeoutMs_)) { break; }
    }
    buffer += socket.readAll();
    socket.abort();

    if (buffer.isEmpty()) {
        qCWarning(lcMoonlightRtsp) << "host closed the connection during"
                                   << QString::fromStdString(stage_) << "before answering";
        return std::nullopt;
    }
    auto response = ml::parseRtspResponse(buffer.toStdString());
    if (!response.has_value()) {
        qCWarning(lcMoonlightRtsp)
            << "unparsable reply to" << QString::fromStdString(stage_) << ":" << escaped(buffer);
        return std::nullopt;
    }
    if (response->statusCode < 200 || response->statusCode > 299) {
        qCWarning(lcMoonlightRtsp)
            << "<-" << QString::fromStdString(stage_) << "refused:" << response->statusCode;
        return std::nullopt;
    }
    qCDebug(lcMoonlightRtsp) << "<-" << QString::fromStdString(stage_) << response->statusCode
                             << "with" << static_cast<int>(response->options.size()) << "options,"
                             << static_cast<int>(response->payloads.size()) << "payload lines";
    return response;
}

std::optional<ml::RtspResponse> MoonlightRtspClient::setup(const std::string& streamId) {
    auto response =
        send("SETUP", "streamid=" + streamId,
             {{"Transport", "unicast;X-GS-ClientPort=" + streamId}, {"X-GS-ClientVersion", "14"}});
    if (response.has_value() && !ml::setupServerPort(*response).has_value()) {
        qCWarning(lcMoonlightRtsp)
            << "SETUP" << QString::fromStdString(streamId) << "carried no server_port";
    }
    return response;
}

// The three SETUPs and what their replies say. Only the control port is required: audio and video
// are negotiated because the host expects to be asked, but Dish decodes neither, so a host that
// names no port for them is not a failed handshake.
std::optional<RtspHandshakeResult> MoonlightRtspClient::negotiateStreams() {
    const auto audio = setup("audio");
    const auto video = setup("video");
    const auto control = setup("control");
    if (!audio.has_value() || !video.has_value() || !control.has_value()) { return std::nullopt; }

    const auto controlPort = ml::setupServerPort(*control);
    if (!controlPort.has_value()) { return std::nullopt; }

    RtspHandshakeResult result;
    result.audioPort = static_cast<std::uint16_t>(ml::setupServerPort(*audio).value_or(0));
    result.videoPort = static_cast<std::uint16_t>(ml::setupServerPort(*video).value_or(0));
    result.controlPort = static_cast<std::uint16_t>(*controlPort);
    result.connectData = ml::setupConnectData(*control).value_or(0);

    // Each stream's ping stands in for the other's when a host names only one, which is what
    // Sunshine does. Both empty is the legacy case the caller warns about.
    result.audioPingPayload = ml::setupPingPayload(*audio);
    result.videoPingPayload = ml::setupPingPayload(*video);
    if (result.videoPingPayload.empty()) { result.videoPingPayload = result.audioPingPayload; }
    if (result.audioPingPayload.empty()) { result.audioPingPayload = result.videoPingPayload; }
    return result;
}

std::optional<RtspHandshakeResult> MoonlightRtspClient::handshake(int width, int height, int fps) {
    const std::string target = "rtsp://" + host_ + ":" + std::to_string(rtspPort_);

    if (!send("OPTIONS", target, {clientVersionHeader()}).has_value()) { return std::nullopt; }
    if (!send("DESCRIBE", target, {clientVersionHeader(), {"Accept", "application/sdp"}})
             .has_value()) {
        return std::nullopt;
    }

    // Not const: it is returned, and a const local cannot be moved out of.
    auto result = negotiateStreams();
    if (!result.has_value()) { return std::nullopt; }

    const std::string sdp = ml::buildAnnounceSdp(width, height, fps);
    if (!send("ANNOUNCE", target, {{"Content-type", "application/sdp"}, sessionHeader()}, sdp)
             .has_value()) {
        return std::nullopt;
    }
    if (!send("PLAY", target, {sessionHeader()}).has_value()) { return std::nullopt; }

    qCInfo(lcMoonlightRtsp) << "negotiated ports on" << QString::fromStdString(host_) << ": control"
                            << result->controlPort << "video" << result->videoPort << "audio"
                            << result->audioPort << "; connect-data" << result->connectData
                            << "; ping payload" << static_cast<int>(result->videoPingPayload.size())
                            << "chars";
    if (result->videoPingPayload.empty()) {
        qCWarning(lcMoonlightRtsp)
            << "host named no ping payload; falling back to the legacy 4-byte media ping";
    }
    return result;
}

} // namespace dish::net
