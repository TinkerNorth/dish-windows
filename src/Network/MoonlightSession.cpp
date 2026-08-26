// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightSession.h"

#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightPairing.h"
#include "repository/MoonlightHostRepository.h"

#include <QGuiApplication>
#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QScreen>
#include <QSize>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

namespace dish::net {

Q_LOGGING_CATEGORY(lcMoonlightSession, "dish.moonlight.session")

namespace {

// Moonlight clients send a fixed 16-hex uniqueid; the host disambiguates by IP.
const QString kUniqueId = QStringLiteral("0123456789ABCDEF");

std::array<std::uint8_t, 16> toKey16(const moonlight::crypto::Bytes& b) {
    std::array<std::uint8_t, 16> k{};
    for (std::size_t i = 0; i < 16 && i < b.size(); ++i) { k[i] = b[i]; }
    return k;
}

struct DisplayMode {
    int width = 1280;
    int height = 720;
    int fps = 30;
};

// The mode to ask the host for. ASK FOR THE DISPLAY WE HAVE, not the smallest
// mode the protocol will express. Dish decodes nothing, so a small mode looks
// like the frugal choice, but a host that materialises a virtual display sized
// to the request (Apollo, Vibepollo) would then resize the desktop of the person
// sitting in front of it, which is exactly who this client is for. `sops=0`
// keeps a real-display host from changing modes either way.
DisplayMode requestedDisplayMode() {
    DisplayMode mode;
    const QScreen* screen = QGuiApplication::primaryScreen();
    if (screen == nullptr) { return mode; }
    // QScreen reports device-independent pixels; the host wants the real ones.
    const QSize logical = screen->size();
    const auto scale = screen->devicePixelRatio();
    const int width = static_cast<int>(logical.width() * scale);
    const int height = static_cast<int>(logical.height() * scale);
    if (width > 0 && height > 0) {
        mode.width = width;
        mode.height = height;
    }
    const qreal hz = screen->refreshRate();
    if (hz >= 1.0) { mode.fps = static_cast<int>(hz + 0.5); }
    return mode;
}

} // namespace

MoonlightSession::MoonlightSession(models::MoonlightHost host, moonlight::Identity identity,
                                   repository::MoonlightHostRepository* repo, QObject* parent)
    : QObject(parent), host_(std::move(host)), identity_(std::move(identity)), repo_(repo),
      http_(new MoonlightHttpClient(this)) {
    http_->setClientIdentity(identity_);
    // TOFU pin: accept-and-remember the server cert first seen, reject a change.
    http_->setPinVerifier([this](const QString&, const QByteArray& der) {
        if (der.isEmpty() || repo_ == nullptr) { return true; }
        const QString id = host_.id();
        const QString presented = QString::fromUtf8(der.toHex());
        const auto stored = repo_->serverCert(id);
        if (!stored.has_value()) {
            qCInfo(lcMoonlightSession)
                << host_.ip << "pinning its server certificate on first sight";
            repo_->setServerCert(id, presented);
            serverCertMismatch_ = false;
            return true;
        }
        if (*stored == presented) {
            serverCertMismatch_ = false;
            return true;
        }
        // Refusing here aborts the reply, and an aborted reply is indistinguishable
        // from an unplugged cable everywhere downstream. Recording it is what lets
        // the section say "this host was reset" instead of "check your network".
        serverCertMismatch_ = true;
        qCWarning(lcMoonlightSession)
            << host_.ip << "presented a server certificate that is not the pinned one;"
            << "refusing the connection. The host was reset, or something is in the middle.";
        return false;
    });
    wireControlHandlers();
}

void MoonlightSession::detachFromStore() {
    repo_ = nullptr;
    // Without this the verifier above would re-pin the certificate of a host the
    // caller is in the middle of forgetting, because /cancel goes out over TLS.
    http_->setPinVerifier(nullptr);
    qCInfo(lcMoonlightSession) << host_.ip << "detached from the store; nothing more is persisted";
}

MoonlightSession::~MoonlightSession() {
    quit();
    if (worker_.joinable()) { worker_.join(); }
}

void MoonlightSession::wireControlHandlers() {
    control_.setRumbleHandler([this](const moonlight::RumbleEvent& e) {
        // Marshal to the session's thread; UI routing must not run on the ENet
        // receive thread.
        QMetaObject::invokeMethod(
            this, [this, e] { emit rumbleReceived(e.controllerNumber, e.lowFreq, e.highFreq); },
            Qt::QueuedConnection);
    });
    control_.setRgbLedHandler([this](const moonlight::RgbLedEvent& e) {
        QMetaObject::invokeMethod(
            this, [this, e] { emit rgbLedReceived(e.controllerNumber, e.r, e.g, e.b); },
            Qt::QueuedConnection);
    });
    control_.setMotionRequestHandler([this](const moonlight::MotionRequestEvent& e) {
        QMetaObject::invokeMethod(
            this,
            [this, e] { emit motionRequested(e.controllerNumber, e.reportRateHz, e.motionType); },
            Qt::QueuedConnection);
    });
    control_.setDisconnectHandler([this](bool terminated) {
        QMetaObject::invokeMethod(
            this,
            [this, terminated] {
                dispatch(terminated ? moonlight::SessionEvent::ServerTerminated
                                    : moonlight::SessionEvent::ControlDropped);
            },
            Qt::QueuedConnection);
    });
}

void MoonlightSession::dispatch(moonlight::SessionEvent event) {
    const auto r = moonlight::reduceSession(state_, event);
    if (r.next.has_value()) {
        qCInfo(lcMoonlightSession)
            << host_.ip << "phase" << static_cast<int>(state_.phase) << "->"
            << static_cast<int>(r.next->phase) << "on event" << static_cast<int>(event) << "failure"
            << static_cast<int>(r.next->failure);
        state_ = *r.next;
        emit phaseChanged();
    }
    runEffects(r.effects);
}

bool MoonlightSession::streaming() const {
    return state_.phase == moonlight::SessionPhase::Streaming ||
           state_.phase == moonlight::SessionPhase::Faltering;
}

void MoonlightSession::runEffects(const std::vector<moonlight::SessionEffect>& effects) {
    for (auto e : effects) {
        switch (e) {
        case moonlight::SessionEffect::BeginLaunch:
            beginLaunch();
            break;
        case moonlight::SessionEffect::BeginRtsp:
            beginRtspAndControl();
            break;
        case moonlight::SessionEffect::StartPinging:
            if (pingTimer_ == nullptr) {
                pingTimer_ = new QTimer(this);
                pingTimer_->setInterval(500);
                QObject::connect(pingTimer_, &QTimer::timeout, this, &MoonlightSession::onPingTick);
            }
            // The FIRST RTP ping must go out immediately after PLAY: a host that
            // gates media startup on media-port liveness would otherwise sit for
            // a whole tick before seeing the client's address.
            onPingTick();
            pingTimer_->start();
            break;
        case moonlight::SessionEffect::StopPinging:
            if (pingTimer_ != nullptr) { pingTimer_->stop(); }
            break;
        case moonlight::SessionEffect::SendArrival:
            sendPendingArrivals();
            break;
        case moonlight::SessionEffect::Teardown:
            if (pingTimer_ != nullptr) { pingTimer_->stop(); }
            closeMediaSockets();
            control_.disconnect();
            qCInfo(lcMoonlightSession)
                << host_.ip << "tearing the session down, cancelling the app";
            cancelHostApp();
            break;
        case moonlight::SessionEffect::BeginPairing:
        case moonlight::SessionEffect::ConnectControl:
        case moonlight::SessionEffect::NotifyFailure:
            // BeginPairing/ConnectControl are driven inline by pair() and the
            // RTSP worker; failure is surfaced through phaseChanged(),
            // failure() and failureMessage().
            break;
        }
    }
}

void MoonlightSession::closeMediaSockets() {
    for (QUdpSocket** socket : {&videoPingSocket_, &audioPingSocket_}) {
        if (*socket == nullptr) { continue; }
        (*socket)->close();
        (*socket)->deleteLater();
        *socket = nullptr;
    }
}

void MoonlightSession::cancelHostApp() {
    http_->getHttps(host_.ip, host_.httpsPort, QStringLiteral("/cancel"),
                    {{QStringLiteral("uniqueid"), kUniqueId}},
                    [this](const MoonlightXmlResponse& r) {
                        qCInfo(lcMoonlightSession) << host_.ip << "/cancel reachable" << r.reachable
                                                   << "status" << r.statusCode << r.statusMessage;
                    });
}

void MoonlightSession::pair(const QString& pin) {
    if (repo_ == nullptr) {
        emit pairingFinished(false);
        return;
    }
    dispatch(moonlight::SessionEvent::StartPairing);

    auto pc = std::make_shared<moonlight::PairingClient>(
        identity_, pin.toStdString(), moonlight::crypto::randomBytes(16),
        moonlight::crypto::randomBytes(16), moonlight::crypto::randomBytes(16));
    if (!pc->valid()) {
        dispatch(moonlight::SessionEvent::PairFailed);
        emit pairingFinished(false);
        return;
    }

    // Each phase chains into the next in its callback. A failure at any step
    // routes to PairFailed, naming the phase and whatever the host said in the
    // body: a /pair phase refuses the same way /launch does, with HTTP 200 and a
    // status_code of its own. The lambdas capture `pc` (shared) so state
    // survives.
    // Every phase carries the generation it started in, so a cancel between two
    // phases stops the chain instead of reporting a refusal the host never made.
    const unsigned generation = pairGeneration_;
    auto abandoned = [this, generation](const char* phase) {
        if (pairGeneration_ == generation) { return false; }
        qCInfo(lcMoonlightSession)
            << host_.ip << "pairing was cancelled; dropping the reply to" << phase;
        return true;
    };

    auto fail = [this](const char* phase, const MoonlightXmlResponse& r) {
        failureMessage_ = r.statusMessage;
        qCWarning(lcMoonlightSession) << host_.ip << "pairing gave up at" << phase << ": reachable"
                                      << r.reachable << "status" << r.statusCode << r.statusMessage;
        dispatch(moonlight::SessionEvent::PairFailed);
        emit pairingFinished(false);
    };

    const QString ip = host_.ip;
    const int httpPort = host_.httpPort;
    const int httpsPort = host_.httpsPort;

    // Phase 1. `phrase=getservercert` is what marks it AS phase 1: a host that
    // does not see it looks the uniqueid up in its pending-pairing table
    // instead, finds nothing, and answers `400 Invalid uniqueid` in the body,
    // which is what a live Sunshine host did to every attempt without it.
    http_->getHttp(
        ip, httpPort, QStringLiteral("/pair"),
        {{QStringLiteral("uniqueid"), kUniqueId},
         {QStringLiteral("devicename"), QStringLiteral("Dish")},
         {QStringLiteral("updateState"), QStringLiteral("1")},
         {QStringLiteral("phrase"), QStringLiteral("getservercert")},
         {QStringLiteral("salt"), QString::fromStdString(pc->saltHex())},
         {QStringLiteral("clientcert"), QString::fromStdString(pc->clientCertHex())}},
        [this, pc, fail, abandoned, ip, httpPort, httpsPort](const MoonlightXmlResponse& r1) {
            if (abandoned("phase 1")) { return; }
            const std::string plaincert = r1.value(QStringLiteral("plaincert")).toStdString();
            if (!r1.reachable || !r1.ok() || plaincert.empty() ||
                !pc->consumeServerCert(plaincert)) {
                fail("phase 1 (salt + clientcert)", r1);
                return;
            }
            // Phase 2
            http_->getHttp(
                ip, httpPort, QStringLiteral("/pair"),
                {{QStringLiteral("uniqueid"), kUniqueId},
                 {QStringLiteral("clientchallenge"),
                  QString::fromStdString(pc->clientChallengeHex())}},
                [this, pc, fail, abandoned, ip, httpPort,
                 httpsPort](const MoonlightXmlResponse& r2) {
                    if (abandoned("phase 2")) { return; }
                    const std::string cr =
                        r2.value(QStringLiteral("challengeresponse")).toStdString();
                    if (!r2.reachable || !r2.ok() || cr.empty() ||
                        !pc->consumeChallengeResponse(cr)) {
                        fail("phase 2 (clientchallenge)", r2);
                        return;
                    }
                    // Phase 3
                    http_->getHttp(
                        ip, httpPort, QStringLiteral("/pair"),
                        {{QStringLiteral("uniqueid"), kUniqueId},
                         {QStringLiteral("serverchallengeresp"),
                          QString::fromStdString(pc->serverChallengeRespHex())}},
                        [this, pc, fail, abandoned, ip, httpPort,
                         httpsPort](const MoonlightXmlResponse& r3) {
                            if (abandoned("phase 3")) { return; }
                            const std::string ps =
                                r3.value(QStringLiteral("pairingsecret")).toStdString();
                            if (!r3.reachable || !r3.ok() || ps.empty() ||
                                !pc->consumePairingSecret(ps)) {
                                fail("phase 3 (serverchallengeresp)", r3);
                                return;
                            }
                            // Phase 4
                            http_->getHttp(
                                ip, httpPort, QStringLiteral("/pair"),
                                {{QStringLiteral("uniqueid"), kUniqueId},
                                 {QStringLiteral("clientpairingsecret"),
                                  QString::fromStdString(pc->clientPairingSecretHex())}},
                                [this, pc, fail, abandoned, ip,
                                 httpsPort](const MoonlightXmlResponse& r4) {
                                    if (abandoned("phase 4")) { return; }
                                    if (!r4.reachable || !r4.ok() || !r4.paired()) {
                                        fail("phase 4 (clientpairingsecret)", r4);
                                        return;
                                    }
                                    // Phase 5 (HTTPS, presents the client cert)
                                    http_->getHttps(
                                        ip, httpsPort, QStringLiteral("/pair"),
                                        {{QStringLiteral("uniqueid"), kUniqueId},
                                         {QStringLiteral("phrase"),
                                          QStringLiteral("pairchallenge")}},
                                        [this, pc, fail,
                                         abandoned](const MoonlightXmlResponse& r5) {
                                            if (abandoned("phase 5")) { return; }
                                            if (!r5.reachable || !r5.ok() || !r5.paired()) {
                                                fail("phase 5 (pairchallenge over TLS)", r5);
                                                return;
                                            }
                                            host_.paired = true;
                                            if (repo_ != nullptr) { repo_->rememberHost(host_); }
                                            qCInfo(lcMoonlightSession) << host_.ip << "paired";
                                            dispatch(moonlight::SessionEvent::PairSucceeded);
                                            emit pairingFinished(true);
                                        });
                                });
                        });
                });
        });
}

void MoonlightSession::cancelPairing() {
    if (state_.phase != moonlight::SessionPhase::Pairing) { return; }
    ++pairGeneration_;
    qCInfo(lcMoonlightSession) << host_.ip << "pairing cancelled by the user";
    // The phase machine has one way out of Pairing that is not success, and the
    // caller clears the refusal flag the section renders from, so this leaves the
    // wire lifecycle consistent without telling the user the host said no.
    dispatch(moonlight::SessionEvent::PairFailed);
}

void MoonlightSession::launch(const QString& appId) {
    pendingAppId_ = appId;
    dispatch(moonlight::SessionEvent::StartLaunch);
}

void MoonlightSession::beginLaunch() {
    failureMessage_.clear();
    resumeAvailable_ = false;

    // Fresh control-stream key material for this session.
    const auto rikeyBytes = moonlight::crypto::randomBytes(16);
    rikey_ = toKey16(rikeyBytes);
    const auto idBytes = moonlight::crypto::randomBytes(4);
    rikeyId_ = static_cast<std::uint32_t>(idBytes[0]) |
               (static_cast<std::uint32_t>(idBytes[1]) << 8) |
               (static_cast<std::uint32_t>(idBytes[2]) << 16) |
               (static_cast<std::uint32_t>(idBytes[3]) << 24);

    const DisplayMode mode = requestedDisplayMode();
    const QString modeString =
        QStringLiteral("%1x%2x%3").arg(mode.width).arg(mode.height).arg(mode.fps);
    qCInfo(lcMoonlightSession) << host_.ip << "launching app"
                               << (pendingAppId_.isEmpty() ? QStringLiteral("<default>")
                                                           : pendingAppId_)
                               << "at" << modeString;

    requestSession(
        QStringLiteral("/launch"),
        {{QStringLiteral("uniqueid"), kUniqueId},
         {QStringLiteral("appid"), pendingAppId_.isEmpty() ? QStringLiteral("1") : pendingAppId_},
         {QStringLiteral("mode"), modeString},
         {QStringLiteral("additionalStates"), QStringLiteral("1")},
         // The host must not change the display it is showing its own user.
         {QStringLiteral("sops"), QStringLiteral("0")},
         {QStringLiteral("rikey"),
          QString::fromStdString(moonlight::crypto::hexEncode(rikeyBytes))},
         {QStringLiteral("rikeyid"), QString::number(rikeyId_)},
         // KEEP THE HOST'S SPEAKERS ALIVE. Mode 0 asks the host to mute itself
         // for the duration, which is right for a remote viewer and wrong for
         // Dish, whose user is sitting at the host using this as a pad: it would
         // silence the very machine they are listening to.
         {QStringLiteral("localAudioPlayMode"), QStringLiteral("1")},
         {QStringLiteral("surroundAudioInfo"), QStringLiteral("196610")},
         {QStringLiteral("remoteControllersBitmap"), QStringLiteral("1")},
         {QStringLiteral("gcmap"), QStringLiteral("1")}});
}

void MoonlightSession::requestSession(const QString& path,
                                      const std::map<QString, QString>& query) {
    const bool resuming = path == QLatin1String("/resume");
    http_->getHttps(
        host_.ip, host_.httpsPort, path, query,
        [this, resuming](const MoonlightXmlResponse& r) { onLaunchReply(r, resuming); });
}

void MoonlightSession::onLaunchReply(const MoonlightXmlResponse& r, bool resuming) {
    if (!r.reachable) {
        qCWarning(lcMoonlightSession) << host_.ip << "session request did not reach the host";
        dispatch(moonlight::SessionEvent::Unreachable);
        return;
    }

    // THE HOST SAYS NO IN THE BODY. A refusal arrives as HTTP 200 carrying a
    // status_code of its own, so the transport status proves nothing.
    if (!r.ok()) {
        failureMessage_ = r.statusMessage;
        resumeAvailable_ = r.resumeAvailable;
        qCWarning(lcMoonlightSession) << host_.ip << "refused the session:" << r.statusCode
                                      << r.statusMessage << "resume" << r.resumeAvailable;
        if (resuming) {
            // The host named this session ours to take back and then would not
            // hand it over. There is nothing left to try but closing it.
            qCWarning(lcMoonlightSession) << host_.ip << "refused the resume it offered";
            dispatch(moonlight::SessionEvent::ResumeRefused);
            return;
        }
        if (r.appAlreadyRunning() && r.resumeAvailable) {
            qCInfo(lcMoonlightSession) << host_.ip << "app already running and resumable, resuming";
            requestSession(
                QStringLiteral("/resume"),
                {{QStringLiteral("uniqueid"), kUniqueId},
                 {QStringLiteral("rikey"), QString::fromStdString(moonlight::crypto::hexEncode(
                                               rikey_.data(), rikey_.size()))},
                 {QStringLiteral("rikeyid"), QString::number(rikeyId_)},
                 {QStringLiteral("surroundAudioInfo"), QStringLiteral("196610")}});
            return;
        }
        dispatch(r.appAlreadyRunning() ? moonlight::SessionEvent::LaunchRefusedBusy
                                       : moonlight::SessionEvent::LaunchFailed);
        return;
    }

    const QString sessionUrl = r.value(QStringLiteral("sessionUrl0"));
    int rtspPort = 48010;
    if (!sessionUrl.isEmpty()) {
        const QUrl url(sessionUrl);
        if (url.port() > 0) { rtspPort = url.port(); }
    }
    rtspTarget_ = QStringLiteral("%1:%2").arg(host_.ip).arg(rtspPort);
    qCInfo(lcMoonlightSession) << host_.ip << "session accepted, RTSP at" << rtspTarget_
                               << "gamesession" << r.value(QStringLiteral("gamesession"))
                               << "resume" << r.value(QStringLiteral("resume"));

    if (r.value(QStringLiteral("gamesession")) == QLatin1String("1") ||
        r.value(QStringLiteral("resume")) == QLatin1String("1")) {
        dispatch(moonlight::SessionEvent::LaunchSucceeded);
    } else {
        failureMessage_ = r.statusMessage;
        qCWarning(lcMoonlightSession)
            << host_.ip << "session reply named neither a gamesession nor a resume";
        dispatch(moonlight::SessionEvent::LaunchFailed);
    }
}

void MoonlightSession::beginRtspAndControl() {
    if (worker_.joinable()) { worker_.join(); }
    const QString target = rtspTarget_;
    const std::string ip = host_.ip.toStdString();
    const auto rikey = rikey_;
    const DisplayMode mode = requestedDisplayMode();

    worker_ = std::thread([this, target, ip, rikey, mode] {
        // target is "ip:port".
        std::uint16_t rtspPort = 48010;
        const int colon = target.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) { rtspPort = static_cast<std::uint16_t>(target.mid(colon + 1).toUShort()); }

        MoonlightRtspClient rtsp(ip, rtspPort);
        const auto result = rtsp.handshake(mode.width, mode.height, mode.fps);
        if (!result.has_value() || result->controlPort == 0) {
            const QString stage = QString::fromStdString(rtsp.lastStage());
            QMetaObject::invokeMethod(
                this,
                [this, stage] {
                    failureMessage_ = stage;
                    onRtspFinished(false, false, RtspHandshakeResult{});
                },
                Qt::QueuedConnection);
            return;
        }
        const bool ok = control_.connect(ip, result->controlPort, rikey, result->connectData);
        const RtspHandshakeResult handshake = *result;
        QMetaObject::invokeMethod(
            this, [this, ok, handshake] { onRtspFinished(true, ok, handshake); },
            Qt::QueuedConnection);
    });
}

void MoonlightSession::onRtspFinished(bool rtspOk, bool controlOk,
                                      const RtspHandshakeResult& rtsp) {
    // Held for onPingTick: the media ports and their SETUP ping payloads.
    rtsp_ = rtsp;
    rtpPingSeq_ = 0;
    if (!rtspOk) {
        qCWarning(lcMoonlightSession) << host_.ip << "RTSP handshake gave up at" << failureMessage_;
        dispatch(moonlight::SessionEvent::RtspFailed);
        return;
    }
    dispatch(moonlight::SessionEvent::RtspSucceeded);
    if (!controlOk) {
        qCWarning(lcMoonlightSession)
            << host_.ip << "ENet control connect to port" << rtsp.controlPort << "failed";
    }
    dispatch(controlOk ? moonlight::SessionEvent::ControlConnected
                       : moonlight::SessionEvent::ControlConnectFailed);
}

void MoonlightSession::onPingTick() {
    // 1) The encrypted control-stream keepalive.
    control_.sendPeriodicPing();

    // 2) The RTP client pings. Sunshine and Wolf both learn the client's media
    //    address from these datagrams and will not start (or will time out) a
    //    stream whose ports never saw one, so they are re-sent every tick rather
    //    than only once. Failing this ends the session ten seconds after PLAY
    //    with `Initial Ping Timeout`. We never decode media: anything the host
    //    sends back is drained and dropped below.
    if (rtsp_.videoPort == 0 && rtsp_.audioPort == 0) { return; }

    const QHostAddress dest(host_.ip);
    auto ping = [&](QUdpSocket*& socket, std::uint16_t port, const std::string& payload) {
        if (port == 0) { return; }
        if (socket == nullptr) {
            socket = new QUdpSocket(this);
            // Any local port: the SS_PING form is matched by its payload, not by
            // where it came from, and the host answers whatever it observes.
            if (!socket->bind(QHostAddress::AnyIPv4, 0)) {
                socket->deleteLater();
                socket = nullptr;
                return;
            }
            qCDebug(lcMoonlightSession) << host_.ip << "media ping socket for port" << port
                                        << "bound to local port" << socket->localPort();
        }
        const auto datagram = moonlight::encodeRtpPing(payload, rtpPingSeq_);
        socket->writeDatagram(reinterpret_cast<const char*>(datagram.data()),
                              static_cast<qint64>(datagram.size()), dest, port);
        while (socket->hasPendingDatagrams()) { socket->receiveDatagram(0); }
    };
    ping(videoPingSocket_, rtsp_.videoPort, rtsp_.videoPingPayload);
    ping(audioPingSocket_, rtsp_.audioPort, rtsp_.audioPingPayload);
    if (rtpPingSeq_ == 0) {
        qCInfo(lcMoonlightSession) << host_.ip << "media pings started: video" << rtsp_.videoPort
                                   << "audio" << rtsp_.audioPort << "payload"
                                   << static_cast<int>(rtsp_.videoPingPayload.size()) << "chars";
    }
    ++rtpPingSeq_;
}

void MoonlightSession::refreshApps() {
    http_->getHttps(host_.ip, host_.httpsPort, QStringLiteral("/applist"),
                    {{QStringLiteral("uniqueid"), kUniqueId}},
                    [this](const MoonlightXmlResponse& r) {
                        // /applist refuses in the body too: an unpaired client
                        // gets HTTP 200 with a status_code of its own.
                        if (!r.reachable || !r.ok()) {
                            qCWarning(lcMoonlightSession)
                                << host_.ip << "/applist gave nothing back: reachable"
                                << r.reachable << "status" << r.statusCode << r.statusMessage;
                            emit appListReady({}, {}, false, r.unauthorized());
                            return;
                        }
                        QStringList ids;
                        QStringList titles;
                        for (const auto& app : parseMoonlightAppList(r.rawBody)) {
                            ids.append(app.id);
                            titles.append(app.title);
                        }
                        emit appListReady(ids, titles, true, false);
                    });
}

void MoonlightSession::probe() {
    http_->getHttp(
        host_.ip, host_.httpPort, QStringLiteral("/serverinfo"),
        {{QStringLiteral("uniqueid"), kUniqueId}}, [this](const MoonlightXmlResponse& r) {
            const QString uniqueId = r.value(QStringLiteral("uniqueid"));
            const bool paired = r.value(QStringLiteral("PairStatus")) == QLatin1String("1");
            qCInfo(lcMoonlightSession)
                << host_.ip << "/serverinfo reachable" << r.reachable << "PairStatus"
                << r.value(QStringLiteral("PairStatus")) << "uniqueid" << uniqueId;
            emit probeFinished(r.reachable, paired, uniqueId);
        });
}

void MoonlightSession::sendControllerState(const moonlight::ControllerState& state) {
    if (!streaming()) { return; }
    control_.sendControllerState(state);
}

void MoonlightSession::sendControllerArrival(std::uint8_t number, std::uint8_t type,
                                             std::uint8_t caps, std::uint32_t supportedButtons) {
    arrivals_[number] = PadArrival{type, caps, supportedButtons};
    if (!streaming()) {
        qCInfo(lcMoonlightSession)
            << host_.ip << "pad" << number << "announced before the stream is live; held";
        return;
    }
    qCInfo(lcMoonlightSession) << host_.ip << "CONTROLLER_ARRIVAL pad" << number << "type" << type
                               << "caps" << caps << "buttons" << supportedButtons;
    control_.sendControllerArrival(number, type, caps, supportedButtons);
}

void MoonlightSession::forgetControllerArrival(std::uint8_t number) { arrivals_.erase(number); }

void MoonlightSession::sendPendingArrivals() {
    for (const auto& [number, pad] : arrivals_) {
        qCInfo(lcMoonlightSession)
            << host_.ip << "CONTROLLER_ARRIVAL pad" << number << "type" << pad.type << "caps"
            << pad.capabilities << "buttons" << pad.supportedButtons;
        control_.sendControllerArrival(number, pad.type, pad.capabilities, pad.supportedButtons);
    }
}

void MoonlightSession::sendControllerMotion(std::uint8_t number, std::uint8_t motionType, float x,
                                            float y, float z) {
    if (!streaming()) { return; }
    control_.sendControllerMotion(number, motionType, x, y, z);
}

void MoonlightSession::sendControllerBattery(std::uint8_t number, std::uint8_t batteryState,
                                             std::uint8_t percentage) {
    if (!streaming()) { return; }
    control_.sendControllerBattery(number, batteryState, percentage);
}

void MoonlightSession::quit() {
    if (state_.phase == moonlight::SessionPhase::Idle ||
        state_.phase == moonlight::SessionPhase::Closed) {
        return;
    }
    dispatch(moonlight::SessionEvent::UserQuit);
}

} // namespace dish::net
