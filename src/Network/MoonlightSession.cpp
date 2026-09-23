// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightSession.h"

#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightPairing.h"
#include "repository/MoonlightHostRepository.h"

#include <QHostAddress>
#include <QLoggingCategory>
#include <QNetworkDatagram>
#include <QStringView>
#include <QTimer>
#include <QUdpSocket>
#include <QUrl>

#include <array>
#include <utility>

namespace dish::net {

Q_LOGGING_CATEGORY(lcMoonlightSession, "dish.moonlight.session")

namespace {

// Only for a session detached from any store, which no live caller builds: a
// host keys pending pairings and session ownership on the uniqueid, so the
// Moonlight-conventional constant collides the moment two installs pair with
// one host. The real id is minted per install by the repository.
constexpr QStringView kFallbackUniqueId = u"0123456789ABCDEF";

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

// The mode to ask the host for: the smallest a host will take, and the same
// numbers on all three Dish clients. Dish decodes nothing, so every pixel the
// host encodes for this stream is GPU time taken from the game the user is
// playing on that same machine; asking for this client's own screen put a 4K
// request in front of a live Sunshine host and encoder timeouts in its log.
// `sops=0` is what keeps the host from changing the user's display to match,
// so the size asked for here never reaches their desktop.
DisplayMode requestedDisplayMode() { return DisplayMode{}; }

// 5.1 at 16 bits, as sent by every Moonlight client. The host uses it to pick an audio format for
// a stream Dish never decodes; it is sent because a host that is given no number picks stereo and
// then reconfigures its own output device, which is audible to the user sitting at it.
constexpr QStringView kSurroundAudioInfo = u"196610";

} // namespace

int rtspPortFromSessionUrl(const QString& sessionUrl) {
    const QUrl url(sessionUrl);
    return url.port() > 0 ? url.port() : kDefaultRtspPort;
}

QString rtspTargetFor(const QString& hostIp, const QString& sessionUrl) {
    return QStringLiteral("%1:%2").arg(hostIp).arg(rtspPortFromSessionUrl(sessionUrl));
}

MoonlightSession::MoonlightSession(models::MoonlightHost host, moonlight::Identity identity,
                                   repository::MoonlightHostRepository* repo, QObject* parent)
    : QObject(parent), host_(std::move(host)), identity_(std::move(identity)), repo_(repo),
      http_(new MoonlightHttpClient(this)) {
    uniqueId_ = repo_ != nullptr ? repo_->getOrCreateUniqueId() : kFallbackUniqueId.toString();
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
    // The worker first: it may be inside the ENet handshake on control_, which
    // the teardown below cuts. Then the wire, and only the wire. No /cancel from
    // a destructor: the client it would go out on is a child of this object and
    // dies in the same breath, so it never left; and closing Dish is not an
    // unbind, so the app the host is running is left for the next session to
    // resume, which is what the other two clients leave it for.
    if (worker_.joinable()) { worker_.join(); }
    if (pingTimer_ != nullptr) { pingTimer_->stop(); }
    control_.disconnect();
}

// Mixed on THIS thread, where both rumble streams land, so the two can never interleave into a
// torn mix. Only the result is marshalled, which keeps the UI routing off the ENet receive thread.
void MoonlightSession::publishRumbleMix(std::uint16_t controllerNumber,
                                        const moonlight::BodyRumble& mixed) {
    QMetaObject::invokeMethod(
        this,
        [this, n = static_cast<int>(controllerNumber), mixed] {
            emit rumbleReceived(n, mixed.strong, mixed.weak);
        },
        Qt::QueuedConnection);
}

void MoonlightSession::onRumbleEvent(const moonlight::RumbleEvent& e) {
    const auto mixed = updateRumbleMix(e.controllerNumber, [&e](moonlight::RumbleMix mix) {
        return moonlight::withBodyRumble(mix, e.lowFreq, e.highFreq);
    });
    publishRumbleMix(e.controllerNumber, mixed);
}

// No pad this client can claim has trigger motors, so the host's trigger stream folds onto the
// body motors instead of being dropped. The fold and the reason live in
// core/moonlight/MoonlightTriggerRumble.h.
void MoonlightSession::onRumbleTriggerEvent(const moonlight::RumbleTriggerEvent& e) {
    const auto mixed = updateRumbleMix(e.controllerNumber, [&e](moonlight::RumbleMix mix) {
        return moonlight::withTriggerRumble(mix, e.left, e.right);
    });
    publishRumbleMix(e.controllerNumber, mixed);
}

void MoonlightSession::onRgbLedEvent(const moonlight::RgbLedEvent& e) {
    QMetaObject::invokeMethod(
        this, [this, e] { emit rgbLedReceived(e.controllerNumber, e.r, e.g, e.b); },
        Qt::QueuedConnection);
}

// The gate is applied on this thread, not the marshalled one: it is what stops motion going out,
// and a queued hop would leave a window where an unsubscribe has arrived but samples still stream.
void MoonlightSession::onMotionRequestEvent(const moonlight::MotionRequestEvent& e) {
    motionGate_.onMotionRequest(e.controllerNumber, e.reportRateHz, e.motionType);
    QMetaObject::invokeMethod(
        this, [this, e] { emit motionRequested(e.controllerNumber, e.reportRateHz, e.motionType); },
        Qt::QueuedConnection);
}

// A host that said it was ending and a link that simply went are two events, because only one of
// them means the app on the other side is gone.
void MoonlightSession::onControlDisconnect(bool terminated) {
    QMetaObject::invokeMethod(
        this,
        [this, terminated] {
            dispatch(terminated ? moonlight::SessionEvent::ServerTerminated
                                : moonlight::SessionEvent::ControlDropped);
        },
        Qt::QueuedConnection);
}

void MoonlightSession::wireControlHandlers() {
    control_.setRumbleHandler([this](const moonlight::RumbleEvent& e) { onRumbleEvent(e); });
    control_.setRumbleTriggerHandler(
        [this](const moonlight::RumbleTriggerEvent& e) { onRumbleTriggerEvent(e); });
    control_.setRgbLedHandler([this](const moonlight::RgbLedEvent& e) { onRgbLedEvent(e); });
    control_.setMotionRequestHandler(
        [this](const moonlight::MotionRequestEvent& e) { onMotionRequestEvent(e); });
    control_.setDisconnectHandler([this](bool terminated) { onControlDisconnect(terminated); });
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
            // Normally already running since SETUP named the ports; this is the
            // control ping joining in, and a start for the path that never
            // heard the ports named first.
            startPinging();
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
            qCInfo(lcMoonlightSession) << host_.ip << "tearing our end of the session down";
            break;
        case moonlight::SessionEffect::CancelOnHost:
            qCInfo(lcMoonlightSession) << host_.ip << "asking the host to close the app we started";
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

void MoonlightSession::startPinging() {
    if (pingTimer_ == nullptr) {
        pingTimer_ = new QTimer(this);
        pingTimer_->setInterval(500);
        QObject::connect(pingTimer_, &QTimer::timeout, this, &MoonlightSession::onPingTick);
    }
    // The FIRST RTP ping goes out now, not a tick from now: a host that gates
    // media startup on media-port liveness would otherwise sit for a whole tick
    // before seeing the client's address.
    onPingTick();
    if (!pingTimer_->isActive()) { pingTimer_->start(); }
}

void MoonlightSession::onRtspNamedPorts(const RtspHandshakeResult& rtsp) {
    // Only while the handshake that named them is still the current one: a quit
    // that landed in between has torn the media sockets down already.
    if (state_.phase != moonlight::SessionPhase::RtspHandshake) { return; }
    rtsp_ = rtsp;
    rtpPingSeq_ = 0;
    startPinging();
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
    ++cancelsInFlight_;
    http_->getHttps(host_.ip, host_.httpsPort, QStringLiteral("/cancel"),
                    {{QStringLiteral("uniqueid"), uniqueId_}},
                    [this](const MoonlightXmlResponse& r) {
                        qCInfo(lcMoonlightSession) << host_.ip << "/cancel reachable" << r.reachable
                                                   << "status" << r.statusCode << r.statusMessage;
                        if (--cancelsInFlight_ == 0) { emit cancelSettled(); }
                    });
}

// What every phase of a run needs: the client whose state the phases build up, the host's two
// ports, and the generation the run started in.
struct MoonlightSession::PairingRun {
    std::shared_ptr<moonlight::PairingClient> pc;
    unsigned generation = 0;
    QString ip;
    int httpPort = 0;
    int httpsPort = 0;
};

// True when a cancel landed between two phases. Without this the chain would report a refusal the
// host never made, because the reply to the phase already in flight still arrives.
bool MoonlightSession::pairAbandoned(const PairingRun& run, const char* phase) const {
    if (pairGeneration_ == run.generation) { return false; }
    qCInfo(lcMoonlightSession) << host_.ip << "pairing was cancelled; dropping the reply to"
                               << phase;
    return true;
}

// A /pair phase refuses the same way /launch does, with HTTP 200 and a status_code of its own, so
// the body is what names the reason.
void MoonlightSession::pairFailed(const char* phase, const MoonlightXmlResponse& r) {
    failureMessage_ = r.statusMessage;
    qCWarning(lcMoonlightSession) << host_.ip << "pairing gave up at" << phase << ": reachable"
                                  << r.reachable << "status" << r.statusCode << r.statusMessage;
    dispatch(moonlight::SessionEvent::PairFailed);
    emit pairingFinished(false);
}

// `phrase=getservercert` is what marks this AS phase 1: a host that does not see it looks the
// uniqueid up in its pending-pairing table instead, finds nothing, and answers `400 Invalid
// uniqueid` in the body, which is what a live Sunshine host did to every attempt without it.
void MoonlightSession::pairPhase1(const PairingRun& run) {
    http_->getHttp(
        run.ip, run.httpPort, QStringLiteral("/pair"),
        {{QStringLiteral("uniqueid"), uniqueId_},
         {QStringLiteral("devicename"), QStringLiteral("Dish")},
         {QStringLiteral("updateState"), QStringLiteral("1")},
         {QStringLiteral("phrase"), QStringLiteral("getservercert")},
         {QStringLiteral("salt"), QString::fromStdString(run.pc->saltHex())},
         {QStringLiteral("clientcert"), QString::fromStdString(run.pc->clientCertHex())}},
        [this, run](const MoonlightXmlResponse& r) { onPairPhase1(run, r); });
}

void MoonlightSession::onPairPhase1(const PairingRun& run, const MoonlightXmlResponse& r) {
    if (pairAbandoned(run, "phase 1")) { return; }
    const std::string plaincert = r.value(QStringLiteral("plaincert")).toStdString();
    if (!r.reachable || !r.ok() || plaincert.empty() || !run.pc->consumeServerCert(plaincert)) {
        pairFailed("phase 1 (salt + clientcert)", r);
        return;
    }
    pairPhase2(run);
}

void MoonlightSession::pairPhase2(const PairingRun& run) {
    http_->getHttp(
        run.ip, run.httpPort, QStringLiteral("/pair"),
        {{QStringLiteral("uniqueid"), uniqueId_},
         {QStringLiteral("clientchallenge"), QString::fromStdString(run.pc->clientChallengeHex())}},
        [this, run](const MoonlightXmlResponse& r) { onPairPhase2(run, r); });
}

void MoonlightSession::onPairPhase2(const PairingRun& run, const MoonlightXmlResponse& r) {
    if (pairAbandoned(run, "phase 2")) { return; }
    const std::string cr = r.value(QStringLiteral("challengeresponse")).toStdString();
    if (!r.reachable || !r.ok() || cr.empty() || !run.pc->consumeChallengeResponse(cr)) {
        pairFailed("phase 2 (clientchallenge)", r);
        return;
    }
    pairPhase3(run);
}

void MoonlightSession::pairPhase3(const PairingRun& run) {
    http_->getHttp(run.ip, run.httpPort, QStringLiteral("/pair"),
                   {{QStringLiteral("uniqueid"), uniqueId_},
                    {QStringLiteral("serverchallengeresp"),
                     QString::fromStdString(run.pc->serverChallengeRespHex())}},
                   [this, run](const MoonlightXmlResponse& r) { onPairPhase3(run, r); });
}

void MoonlightSession::onPairPhase3(const PairingRun& run, const MoonlightXmlResponse& r) {
    if (pairAbandoned(run, "phase 3")) { return; }
    const std::string ps = r.value(QStringLiteral("pairingsecret")).toStdString();
    if (!r.reachable || !r.ok() || ps.empty() || !run.pc->consumePairingSecret(ps)) {
        pairFailed("phase 3 (serverchallengeresp)", r);
        return;
    }
    pairPhase4(run);
}

void MoonlightSession::pairPhase4(const PairingRun& run) {
    http_->getHttp(run.ip, run.httpPort, QStringLiteral("/pair"),
                   {{QStringLiteral("uniqueid"), uniqueId_},
                    {QStringLiteral("clientpairingsecret"),
                     QString::fromStdString(run.pc->clientPairingSecretHex())}},
                   [this, run](const MoonlightXmlResponse& r) { onPairPhase4(run, r); });
}

void MoonlightSession::onPairPhase4(const PairingRun& run, const MoonlightXmlResponse& r) {
    if (pairAbandoned(run, "phase 4")) { return; }
    if (!r.reachable || !r.ok() || !r.paired()) {
        pairFailed("phase 4 (clientpairingsecret)", r);
        return;
    }
    pairPhase5(run);
}

// Over TLS, presenting the client certificate: this is the phase that proves the key the first
// four phases agreed on is the one the host will accept.
void MoonlightSession::pairPhase5(const PairingRun& run) {
    http_->getHttps(run.ip, run.httpsPort, QStringLiteral("/pair"),
                    {{QStringLiteral("uniqueid"), uniqueId_},
                     {QStringLiteral("phrase"), QStringLiteral("pairchallenge")}},
                    [this, run](const MoonlightXmlResponse& r) { onPairPhase5(run, r); });
}

void MoonlightSession::onPairPhase5(const PairingRun& run, const MoonlightXmlResponse& r) {
    if (pairAbandoned(run, "phase 5")) { return; }
    if (!r.reachable || !r.ok() || !r.paired()) {
        pairFailed("phase 5 (pairchallenge over TLS)", r);
        return;
    }
    host_.paired = true;
    if (repo_ != nullptr) { repo_->rememberHost(host_); }
    qCInfo(lcMoonlightSession) << host_.ip << "paired";
    dispatch(moonlight::SessionEvent::PairSucceeded);
    emit pairingFinished(true);
}

void MoonlightSession::pair(const QString& pin) {
    if (repo_ == nullptr) {
        emit pairingFinished(false);
        return;
    }
    dispatch(moonlight::SessionEvent::StartPairing);

    PairingRun run;
    run.pc = std::make_shared<moonlight::PairingClient>(
        identity_, pin.toStdString(), moonlight::crypto::randomBytes(16),
        moonlight::crypto::randomBytes(16), moonlight::crypto::randomBytes(16));
    if (!run.pc->valid()) {
        dispatch(moonlight::SessionEvent::PairFailed);
        emit pairingFinished(false);
        return;
    }
    run.generation = pairGeneration_;
    run.ip = host_.ip;
    run.httpPort = host_.httpPort;
    run.httpsPort = host_.httpsPort;
    pairPhase1(run);
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
        {{QStringLiteral("uniqueid"), uniqueId_},
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
         {QStringLiteral("surroundAudioInfo"), kSurroundAudioInfo.toString()}});
    // No remoteControllersBitmap and no gcmap, as the other two clients send
    // none: the pads are plugged by their CONTROLLER_ARRIVAL, each with its own
    // type, and a bitmap naming one pad up front is a pad the host may build
    // before it hears what it is.
}

void MoonlightSession::requestSession(const QString& path,
                                      const std::map<QString, QString>& query) {
    const bool resuming = path == QLatin1String("/resume");
    http_->getHttps(
        host_.ip, host_.httpsPort, path, query,
        [this, resuming](const MoonlightXmlResponse& r) { onLaunchReply(r, resuming); });
}

// A refusal arrives as HTTP 200 carrying a status_code of its own, so this runs on a reply the
// transport called a success. `resumeAvailable` is kept because the UI states it separately from
// the failure: a busy host that will hand the session back reads differently from one that will
// not.
void MoonlightSession::onSessionRefused(const MoonlightXmlResponse& r, bool resuming) {
    failureMessage_ = r.statusMessage;
    resumeAvailable_ = r.resumeAvailable;
    qCWarning(lcMoonlightSession) << host_.ip << "refused the session:" << r.statusCode
                                  << r.statusMessage << "resume" << r.resumeAvailable;
    if (resuming) {
        // The host named this session ours to take back and then would not hand it over. There is
        // nothing left to try but closing it.
        qCWarning(lcMoonlightSession) << host_.ip << "refused the resume it offered";
        dispatch(moonlight::SessionEvent::ResumeRefused);
        return;
    }
    if (r.appAlreadyRunning() && r.resumeAvailable) {
        qCInfo(lcMoonlightSession) << host_.ip << "app already running and resumable, resuming";
        requestResume();
        return;
    }
    dispatch(r.appAlreadyRunning() ? moonlight::SessionEvent::LaunchRefusedBusy
                                   : moonlight::SessionEvent::LaunchFailed);
}

// The host answers a launch and a resume the same way, so this is the one reply reader for both.
void MoonlightSession::requestResume() {
    requestSession(QStringLiteral("/resume"),
                   {{QStringLiteral("uniqueid"), uniqueId_},
                    {QStringLiteral("rikey"), QString::fromStdString(moonlight::crypto::hexEncode(
                                                  rikey_.data(), rikey_.size()))},
                    {QStringLiteral("rikeyid"), QString::number(rikeyId_)},
                    {QStringLiteral("surroundAudioInfo"), kSurroundAudioInfo.toString()}});
}

// A 200 that is ok() is still not a session: a host that neither started one nor handed one back
// says so in the body, and treating that as live would leave the client talking RTSP to nothing.
void MoonlightSession::onSessionAccepted(const MoonlightXmlResponse& r) {
    rtspTarget_ = rtspTargetFor(host_.ip, r.value(QStringLiteral("sessionUrl0")));
    qCInfo(lcMoonlightSession) << host_.ip << "session accepted, RTSP at" << rtspTarget_
                               << "gamesession" << r.value(QStringLiteral("gamesession"))
                               << "resume" << r.value(QStringLiteral("resume"));

    if (r.value(QStringLiteral("gamesession")) == QLatin1String("1") ||
        r.value(QStringLiteral("resume")) == QLatin1String("1")) {
        dispatch(moonlight::SessionEvent::LaunchSucceeded);
        return;
    }
    failureMessage_ = r.statusMessage;
    qCWarning(lcMoonlightSession) << host_.ip
                                  << "session reply named neither a gamesession nor a resume";
    dispatch(moonlight::SessionEvent::LaunchFailed);
}

void MoonlightSession::onLaunchReply(const MoonlightXmlResponse& r, bool resuming) {
    if (!r.reachable) {
        qCWarning(lcMoonlightSession) << host_.ip << "session request did not reach the host";
        dispatch(moonlight::SessionEvent::Unreachable);
        return;
    }
    // THE HOST SAYS NO IN THE BODY, so the transport status proves nothing.
    if (!r.ok()) {
        onSessionRefused(r, resuming);
        return;
    }
    onSessionAccepted(r);
}

void MoonlightSession::beginRtspAndControl() {
    if (worker_.joinable()) { worker_.join(); }
    // Captured from non-const locals, so the closure's members are non-const
    // and std::thread moves it into place: a const std::string member would
    // have to be copied instead, and a copy can throw where a move cannot.
    QString target = rtspTarget_;
    std::string ip = host_.ip.toStdString();
    std::array<std::uint8_t, 16> rikey = rikey_;
    DisplayMode mode = requestedDisplayMode();

    worker_ = std::thread([this, target = std::move(target), ip = std::move(ip), rikey, mode] {
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
        // The media ports are pinged from this moment, not from when the ENet
        // handshake below has finished: the host counts its initial-ping
        // deadline from its own session start. Each closure owns a non-const
        // copy of the result, for the same reason as the thread's own captures.
        QMetaObject::invokeMethod(
            this, [this, handshake = *result] { onRtspNamedPorts(handshake); },
            Qt::QueuedConnection);
        const bool ok = control_.connect(ip, result->controlPort, rikey, result->connectData);
        QMetaObject::invokeMethod(
            this, [this, ok, handshake = *result] { onRtspFinished(true, ok, handshake); },
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
                    {{QStringLiteral("uniqueid"), uniqueId_}},
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
    http_->getHttp(host_.ip, host_.httpPort, QStringLiteral("/serverinfo"),
                   {{QStringLiteral("uniqueid"), uniqueId_}},
                   [this](const MoonlightXmlResponse& r) {
                       const QString uniqueId = r.value(QStringLiteral("uniqueid"));
                       // The PairStatus is logged and goes no further. It is a diagnostic
                       // about the host, never an answer about this client's trust.
                       qCInfo(lcMoonlightSession)
                           << host_.ip << "/serverinfo reachable" << r.reachable << "PairStatus"
                           << r.value(QStringLiteral("PairStatus")) << "uniqueid" << uniqueId;
                       emit probeFinished(r.reachable, uniqueId);
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
    sendInitialState(number);
}

std::uint16_t MoonlightSession::presentMask() const {
    std::uint16_t mask = 0;
    for (const auto& [number, pad] : arrivals_) {
        mask = static_cast<std::uint16_t>(mask | (1U << number));
    }
    return mask;
}

void MoonlightSession::sendInitialState(std::uint8_t number) {
    moonlight::ControllerState state;
    state.controllerNumber = number;
    state.activeGamepadMask = presentMask();
    control_.sendControllerState(state);
}

void MoonlightSession::forgetControllerArrival(std::uint8_t number) { arrivals_.erase(number); }

void MoonlightSession::sendPendingArrivals() {
    for (const auto& [number, pad] : arrivals_) {
        qCInfo(lcMoonlightSession)
            << host_.ip << "CONTROLLER_ARRIVAL pad" << number << "type" << pad.type << "caps"
            << pad.capabilities << "buttons" << pad.supportedButtons;
        control_.sendControllerArrival(number, pad.type, pad.capabilities, pad.supportedButtons);
        sendInitialState(number);
    }
}

moonlight::BodyRumble MoonlightSession::updateRumbleMix(
    int controllerNumber, const std::function<moonlight::RumbleMix(moonlight::RumbleMix)>& apply) {
    auto& mix = rumbleMix_[controllerNumber];
    mix = apply(mix);
    return moonlight::mixRumble(mix);
}

bool MoonlightSession::sendControllerMotion(std::uint8_t number, std::uint8_t motionType, float x,
                                            float y, float z) {
    if (!streaming()) { return false; }
    // Steady clock, not wall clock: a system time change must not open the gate
    // for a second or wedge it shut for an hour.
    const auto nowUs = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now().time_since_epoch())
                           .count();
    if (!motionGate_.shouldSend(number, motionType, nowUs)) { return false; }
    control_.sendControllerMotion(number, motionType, x, y, z);
    return true;
}

void MoonlightSession::sendControllerTouch(std::uint8_t number,
                                           const moonlight::TouchEvent& event) {
    if (!streaming()) { return; }
    control_.sendControllerTouch(number, event.eventType, event.pointerId, event.x, event.y,
                                 event.pressure);
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
