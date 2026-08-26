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

std::optional<RtspHandshakeResult> MoonlightRtspClient::handshake(int width, int height, int fps) {
    const std::string target = "rtsp://" + host_ + ":" + std::to_string(rtspPort_);
    const std::map<std::string, std::string> version = {{"X-GS-ClientVersion", "14"}};

    if (!send("OPTIONS", target, version).has_value()) { return std::nullopt; }
    if (!send("DESCRIBE", target, {{"X-GS-ClientVersion", "14"}, {"Accept", "application/sdp"}})
             .has_value()) {
        return std::nullopt;
    }

    RtspHandshakeResult result;

    const auto audio = setup("audio");
    if (!audio.has_value()) { return std::nullopt; }
    const auto video = setup("video");
    if (!video.has_value()) { return std::nullopt; }
    const auto control = setup("control");
    if (!control.has_value()) { return std::nullopt; }

    const auto audioPort = ml::setupServerPort(*audio);
    const auto videoPort = ml::setupServerPort(*video);
    const auto controlPort = ml::setupServerPort(*control);
    if (!controlPort.has_value()) { return std::nullopt; }
    result.audioPort = static_cast<std::uint16_t>(audioPort.value_or(0));
    result.videoPort = static_cast<std::uint16_t>(videoPort.value_or(0));
    result.controlPort = static_cast<std::uint16_t>(*controlPort);
    result.connectData = ml::setupConnectData(*control).value_or(0);
    result.audioPingPayload = ml::setupPingPayload(*audio);
    result.videoPingPayload = ml::setupPingPayload(*video);
    if (result.videoPingPayload.empty()) { result.videoPingPayload = result.audioPingPayload; }
    if (result.audioPingPayload.empty()) { result.audioPingPayload = result.videoPingPayload; }

    const std::string sdp = ml::buildAnnounceSdp(width, height, fps);
    if (!send("ANNOUNCE", target,
              {{"Content-type", "application/sdp"}, {"Session", "DEADBEEFCAFE"}}, sdp)
             .has_value()) {
        return std::nullopt;
    }
    if (!send("PLAY", target, {{"Session", "DEADBEEFCAFE"}}).has_value()) { return std::nullopt; }

    qCInfo(lcMoonlightRtsp) << "negotiated ports on" << QString::fromStdString(host_) << ": control"
                            << result.controlPort << "video" << result.videoPort << "audio"
                            << result.audioPort << "; connect-data" << result.connectData
                            << "; ping payload" << static_cast<int>(result.videoPingPayload.size())
                            << "chars";
    if (result.videoPingPayload.empty()) {
        qCWarning(lcMoonlightRtsp)
            << "host named no ping payload; falling back to the legacy 4-byte media ping";
    }
    return result;
}

} // namespace dish::net
