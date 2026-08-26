// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight session coordinator: the sibling of WifiConnectionManager on the
// Satellite path. It turns world signals into MoonlightSessionMachine events,
// runs the reducer, and executes the resulting effects against the HTTP, RTSP
// and ENet layers. One instance drives one host.
//
// Flow: pair() runs the five HTTP phases; launch() runs /serverinfo -> /launch
// (or /resume) -> the RTSP handshake -> the ENet control connect, then the
// stream is live and sendControllerState() forwards input on the hot path.
// Inbound rumble / trigger / LED / motion-request events are surfaced as Qt
// signals so AppModel can route them through the same plumbing the Satellite
// path uses.
//
// The blocking RTSP + ENet-connect work runs on a worker thread; results are
// marshalled back to this object's thread with a queued signal.

#pragma once

#include "Network/MoonlightControlChannel.h"
#include "Network/MoonlightHost.h"
#include "Network/MoonlightHttpClient.h"
#include "Network/MoonlightRtspClient.h"
#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightSessionMachine.h"

#include <QObject>
#include <QString>

#include <array>
#include <map>
#include <memory>
#include <thread>

namespace dish::repository {
class MoonlightHostRepository;
}

class QTimer;
class QUdpSocket;

namespace dish::net {

class MoonlightSession : public QObject {
    Q_OBJECT
  public:
    MoonlightSession(models::MoonlightHost host, moonlight::Identity identity,
                     repository::MoonlightHostRepository* repo, QObject* parent = nullptr);
    ~MoonlightSession() override;

    const models::MoonlightHost& host() const { return host_; }
    moonlight::SessionPhase phase() const { return state_.phase; }
    moonlight::SessionFailure failure() const { return state_.failure; }
    // The host's own words for the last refusal (`status_message`), empty when
    // the failure did not come from the host's body.
    const QString& failureMessage() const { return failureMessage_; }
    // Whether the last refusal said the running session can be joined instead.
    bool resumeAvailable() const { return resumeAvailable_; }

    // Run the five-phase PIN pairing. Emits pinReady with the client PIN to show
    // (the reverse path is not used here; Moonlight pairing shows the client's
    // own PIN and the operator types it — but Sunshine shows the PIN on the host
    // and the user types it here, so the 4-digit code we pass in is what the user
    // entered). Emits pairingFinished(ok).
    void pair(const QString& pin);

    // Launch (or resume) an app and bring the control stream up. `appId` comes
    // from /applist; empty launches the default "Desktop".
    void launch(const QString& appId);

    // Graceful teardown: TERMINATION + ENet disconnect + /cancel.
    void quit();

    // GET /cancel on its own, with no live session of ours to tear down. This is
    // the answer to a host that refused /launch because an app is already
    // running and offered no <resume>: nothing local is streaming, so quit() has
    // nothing to do, and the app on the host has to be stopped before the next
    // /launch can succeed.
    void cancelHostApp();

    // GET /applist (HTTPS, needs pairing) and emit appListReady.
    void refreshApps();

    // Hot path: forward one controller's state. No-op unless streaming.
    void sendControllerState(const moonlight::ControllerState& state);

    // Announce a virtual pad (its emulated type + caps). The announcement is
    // REMEMBERED as well as sent: a pad can be bound before the stream is live,
    // and the host only materialises a controller it has heard arrive, so every
    // remembered pad is re-announced the moment the control stream comes up.
    void sendControllerArrival(std::uint8_t number, std::uint8_t type, std::uint8_t caps,
                               std::uint32_t supportedButtons);

    // Drop a pad from the remembered set, so a later reconnect does not announce
    // a controller nobody is bound to any more.
    void forgetControllerArrival(std::uint8_t number);

    // Forward a motion sample / battery report for a bound pad. No-ops unless
    // streaming, like sendControllerState.
    void sendControllerMotion(std::uint8_t number, std::uint8_t motionType, float x, float y,
                              float z);
    void sendControllerBattery(std::uint8_t number, std::uint8_t batteryState,
                               std::uint8_t percentage);

  signals:
    void phaseChanged();
    void pairingFinished(bool ok);
    void appListReady(const QStringList& appIds, const QStringList& appTitles);

    // Host -> client events, forwarded for AppModel to route to the local pad.
    void rumbleReceived(int controllerNumber, int lowFreq, int highFreq);
    void rgbLedReceived(int controllerNumber, int r, int g, int b);
    void motionRequested(int controllerNumber, int rateHz, int motionType);

  private:
    void dispatch(moonlight::SessionEvent event);
    void runEffects(const std::vector<moonlight::SessionEffect>& effects);
    void beginLaunch();
    void requestSession(const QString& path, const std::map<QString, QString>& query);
    void onLaunchReply(const MoonlightXmlResponse& r, bool resuming);
    void beginRtspAndControl();
    void onRtspFinished(bool rtspOk, bool controlOk, const RtspHandshakeResult& rtsp);
    void wireControlHandlers();
    void sendPendingArrivals();
    void closeMediaSockets();
    bool streaming() const;
    // One tick of both keepalives: the encrypted PERIODIC_PING on the control
    // stream and the RTP client pings on the negotiated video/audio UDP ports.
    // The host gates media startup on the RTP pings; their incoming payloads are
    // read and discarded (we never decode media).
    void onPingTick();

    // The parsed /launch rikey material.
    std::array<std::uint8_t, 16> rikey_{};
    std::uint32_t rikeyId_ = 0;

    models::MoonlightHost host_;
    moonlight::Identity identity_;
    repository::MoonlightHostRepository* repo_;

    MoonlightHttpClient* http_;
    MoonlightControlChannel control_;
    std::thread worker_;

    moonlight::SessionState state_;
    QString pendingAppId_;
    QString rtspTarget_;
    QString failureMessage_;
    bool resumeAvailable_ = false;
    QTimer* pingTimer_ = nullptr;

    // The pads announced so far, replayed whenever the control stream comes up.
    struct PadArrival {
        std::uint8_t type = 0;
        std::uint8_t capabilities = 0;
        std::uint32_t supportedButtons = 0;
    };
    std::map<std::uint8_t, PadArrival> arrivals_;

    // The negotiated media ports + the SETUP ping payloads, for onPingTick.
    RtspHandshakeResult rtsp_;
    // ONE LONG-LIVED SOCKET PER STREAM. The host learns where to send RTP from
    // the source address of these datagrams, so a throwaway socket per ping
    // names a new port every time and closes the one it just learned.
    QUdpSocket* videoPingSocket_ = nullptr;
    QUdpSocket* audioPingSocket_ = nullptr;
    std::uint32_t rtpPingSeq_ = 0;
};

} // namespace dish::net
