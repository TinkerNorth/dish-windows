// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightSession.h"

#include "Network/MoonlightRtspClient.h"
#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightPairing.h"
#include "repository/MoonlightHostRepository.h"

#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

namespace dish::net {

namespace {

// Moonlight clients send a fixed 16-hex uniqueid; the host disambiguates by IP.
const QString kUniqueId = QStringLiteral("0123456789ABCDEF");

std::array<std::uint8_t, 16> toKey16(const moonlight::crypto::Bytes& b) {
    std::array<std::uint8_t, 16> k{};
    for (std::size_t i = 0; i < 16 && i < b.size(); ++i) { k[i] = b[i]; }
    return k;
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
            repo_->setServerCert(id, presented);
            return true;
        }
        return *stored == presented;
    });
    wireControlHandlers();
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
    control_.setDisconnectHandler([this] {
        QMetaObject::invokeMethod(
            this, [this] { dispatch(moonlight::SessionEvent::ServerTerminated); },
            Qt::QueuedConnection);
    });
}

void MoonlightSession::dispatch(moonlight::SessionEvent event) {
    const auto r = moonlight::reduceSession(state_, event);
    if (r.next.has_value()) {
        state_ = *r.next;
        emit phaseChanged();
    }
    runEffects(r.effects);
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
                QObject::connect(pingTimer_, &QTimer::timeout, this,
                                 [this] { control_.sendPeriodicPing(); });
            }
            pingTimer_->start();
            break;
        case moonlight::SessionEffect::StopPinging:
            if (pingTimer_ != nullptr) { pingTimer_->stop(); }
            break;
        case moonlight::SessionEffect::Teardown:
            if (pingTimer_ != nullptr) { pingTimer_->stop(); }
            control_.disconnect();
            // /cancel is best-effort; ignore the reply.
            http_->getHttps(host_.ip, host_.httpsPort, QStringLiteral("/cancel"),
                            {{QStringLiteral("uniqueid"), kUniqueId}},
                            [](const MoonlightXmlResponse&) {});
            break;
        case moonlight::SessionEffect::BeginPairing:
        case moonlight::SessionEffect::ConnectControl:
        case moonlight::SessionEffect::SendArrival:
        case moonlight::SessionEffect::NotifyFailure:
            // BeginPairing/ConnectControl are driven inline by pair()/the worker;
            // SendArrival is issued by AppModel once it knows the pads; failure is
            // surfaced through phaseChanged() + failure().
            break;
        }
    }
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
    // routes to PairFailed. The lambdas capture `pc` (shared) so state survives.
    auto fail = [this] {
        dispatch(moonlight::SessionEvent::PairFailed);
        emit pairingFinished(false);
    };

    const QString ip = host_.ip;
    const int httpPort = host_.httpPort;
    const int httpsPort = host_.httpsPort;

    // Phase 1
    http_->getHttp(
        ip, httpPort, QStringLiteral("/pair"),
        {{QStringLiteral("uniqueid"), kUniqueId},
         {QStringLiteral("salt"), QString::fromStdString(pc->saltHex())},
         {QStringLiteral("clientcert"), QString::fromStdString(pc->clientCertHex())}},
        [this, pc, fail, ip, httpPort, httpsPort](const MoonlightXmlResponse& r1) {
            const std::string plaincert = r1.value(QStringLiteral("plaincert")).toStdString();
            if (!r1.reachable || plaincert.empty() || !pc->consumeServerCert(plaincert)) {
                fail();
                return;
            }
            // Phase 2
            http_->getHttp(
                ip, httpPort, QStringLiteral("/pair"),
                {{QStringLiteral("uniqueid"), kUniqueId},
                 {QStringLiteral("clientchallenge"),
                  QString::fromStdString(pc->clientChallengeHex())}},
                [this, pc, fail, ip, httpPort, httpsPort](const MoonlightXmlResponse& r2) {
                    const std::string cr =
                        r2.value(QStringLiteral("challengeresponse")).toStdString();
                    if (!r2.reachable || cr.empty() || !pc->consumeChallengeResponse(cr)) {
                        fail();
                        return;
                    }
                    // Phase 3
                    http_->getHttp(
                        ip, httpPort, QStringLiteral("/pair"),
                        {{QStringLiteral("uniqueid"), kUniqueId},
                         {QStringLiteral("serverchallengeresp"),
                          QString::fromStdString(pc->serverChallengeRespHex())}},
                        [this, pc, fail, ip, httpPort, httpsPort](const MoonlightXmlResponse& r3) {
                            const std::string ps =
                                r3.value(QStringLiteral("pairingsecret")).toStdString();
                            if (!r3.reachable || ps.empty() || !pc->consumePairingSecret(ps)) {
                                fail();
                                return;
                            }
                            // Phase 4
                            http_->getHttp(
                                ip, httpPort, QStringLiteral("/pair"),
                                {{QStringLiteral("uniqueid"), kUniqueId},
                                 {QStringLiteral("clientpairingsecret"),
                                  QString::fromStdString(pc->clientPairingSecretHex())}},
                                [this, pc, fail, ip, httpsPort](const MoonlightXmlResponse& r4) {
                                    if (!r4.reachable || !r4.paired()) {
                                        fail();
                                        return;
                                    }
                                    // Phase 5 (HTTPS, presents the client cert)
                                    http_->getHttps(
                                        ip, httpsPort, QStringLiteral("/pair"),
                                        {{QStringLiteral("uniqueid"), kUniqueId},
                                         {QStringLiteral("phrase"),
                                          QStringLiteral("pairchallenge")}},
                                        [this, pc, fail](const MoonlightXmlResponse& r5) {
                                            if (!r5.reachable || !r5.paired()) {
                                                fail();
                                                return;
                                            }
                                            host_.paired = true;
                                            if (repo_ != nullptr) { repo_->rememberHost(host_); }
                                            dispatch(moonlight::SessionEvent::PairSucceeded);
                                            emit pairingFinished(true);
                                        });
                                });
                        });
                });
        });
}

void MoonlightSession::launch(const QString& appId) {
    pendingAppId_ = appId;
    dispatch(moonlight::SessionEvent::StartLaunch);
}

void MoonlightSession::beginLaunch() {
    // Fresh control-stream key material for this session.
    const auto rikeyBytes = moonlight::crypto::randomBytes(16);
    rikey_ = toKey16(rikeyBytes);
    const auto idBytes = moonlight::crypto::randomBytes(4);
    rikeyId_ = static_cast<std::uint32_t>(idBytes[0]) |
               (static_cast<std::uint32_t>(idBytes[1]) << 8) |
               (static_cast<std::uint32_t>(idBytes[2]) << 16) |
               (static_cast<std::uint32_t>(idBytes[3]) << 24);

    std::map<QString, QString> query = {
        {QStringLiteral("uniqueid"), kUniqueId},
        {QStringLiteral("appid"), pendingAppId_.isEmpty() ? QStringLiteral("1") : pendingAppId_},
        // Minimal mode; we discard the media, so lowest settings suffice.
        {QStringLiteral("mode"), QStringLiteral("1280x720x30")},
        {QStringLiteral("additionalStates"), QStringLiteral("1")},
        {QStringLiteral("sops"), QStringLiteral("0")},
        {QStringLiteral("rikey"), QString::fromStdString(moonlight::crypto::hexEncode(rikeyBytes))},
        {QStringLiteral("rikeyid"), QString::number(rikeyId_)},
        {QStringLiteral("localAudioPlayMode"), QStringLiteral("0")},
        {QStringLiteral("surroundAudioInfo"), QStringLiteral("196610")},
        {QStringLiteral("remoteControllersBitmap"), QStringLiteral("1")},
        {QStringLiteral("gcmap"), QStringLiteral("1")}};

    http_->getHttps(host_.ip, host_.httpsPort, QStringLiteral("/launch"), query,
                    [this](const MoonlightXmlResponse& r) {
                        if (!r.reachable) {
                            dispatch(moonlight::SessionEvent::Unreachable);
                            return;
                        }
                        const QString sessionUrl = r.value(QStringLiteral("sessionUrl0"));
                        int rtspPort = 48010;
                        if (!sessionUrl.isEmpty()) {
                            const QUrl url(sessionUrl);
                            if (url.port() > 0) { rtspPort = url.port(); }
                        }
                        // rtsp://<ip>:<port>
                        rtspTarget_ = QStringLiteral("%1:%2").arg(host_.ip).arg(rtspPort);
                        if (r.value(QStringLiteral("gamesession")) == QLatin1String("1") ||
                            r.value(QStringLiteral("resume")) == QLatin1String("1")) {
                            dispatch(moonlight::SessionEvent::LaunchSucceeded);
                        } else {
                            dispatch(moonlight::SessionEvent::LaunchFailed);
                        }
                    });
}

void MoonlightSession::beginRtspAndControl() {
    if (worker_.joinable()) { worker_.join(); }
    const QString target = rtspTarget_;
    const std::string ip = host_.ip.toStdString();
    const auto rikey = rikey_;
    const std::uint32_t rikeyId = rikeyId_;

    worker_ = std::thread([this, target, ip, rikey, rikeyId] {
        // target is "ip:port".
        std::uint16_t rtspPort = 48010;
        const int colon = target.lastIndexOf(QLatin1Char(':'));
        if (colon >= 0) { rtspPort = static_cast<std::uint16_t>(target.mid(colon + 1).toUShort()); }

        MoonlightRtspClient rtsp;
        const auto result = rtsp.handshake(ip, rtspPort, 1280, 720, 30);
        if (!result.has_value() || result->controlPort == 0) {
            QMetaObject::invokeMethod(
                this, [this] { onControlConnected(false, 0); }, Qt::QueuedConnection);
            return;
        }
        const bool ok = control_.connect(ip, result->controlPort, rikey, result->connectData);
        (void)rikeyId;
        const std::uint16_t port = result->controlPort;
        QMetaObject::invokeMethod(
            this, [this, ok, port] { onControlConnected(ok, port); }, Qt::QueuedConnection);
    });
}

void MoonlightSession::onControlConnected(bool ok, std::uint16_t /*controlPort*/) {
    dispatch(ok ? moonlight::SessionEvent::ControlConnected
                : moonlight::SessionEvent::ControlConnectFailed);
}

void MoonlightSession::sendControllerState(const moonlight::ControllerState& state) {
    if (state_.phase != moonlight::SessionPhase::Streaming &&
        state_.phase != moonlight::SessionPhase::Faltering) {
        return;
    }
    control_.sendControllerState(state);
}

void MoonlightSession::sendControllerArrival(std::uint8_t number, std::uint8_t type,
                                             std::uint8_t caps, std::uint32_t supportedButtons) {
    control_.sendControllerArrival(number, type, caps, supportedButtons);
}

void MoonlightSession::quit() {
    if (state_.phase == moonlight::SessionPhase::Idle ||
        state_.phase == moonlight::SessionPhase::Closed) {
        return;
    }
    dispatch(moonlight::SessionEvent::UserQuit);
}

} // namespace dish::net
