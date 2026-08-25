// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight control stream over ENet (UDP). This is the data plane of the
// GameStream path, the sibling of Network/SatelliteClient on the Satellite
// path: it connects to the host's control port, forwards controller state, and
// receives rumble / trigger / motion-request / LED events back.
//
// Hot path: sendControllerState() encodes CONTROLLER_MULTI into a preallocated
// buffer and seals it with a reused GCM context (crypto::ControlSealer), so a
// per-report send does no heap allocation in our code. ENet's own
// enet_packet_create still allocates inside the library; that is inherent to the
// ENet API and out of our control.
//
// The ENet C library is the MIT cgutman fork, vendored via FetchContent; see
// THIRD_PARTY.md.

#pragma once

#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightCrypto.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

struct _ENetHost;
struct _ENetPeer;

namespace dish::net {

// One reference-counted global ENet init, so many channels can coexist.
void moonlightEnetRef();
void moonlightEnetUnref();

class MoonlightControlChannel {
  public:
    MoonlightControlChannel();
    ~MoonlightControlChannel();

    MoonlightControlChannel(const MoonlightControlChannel&) = delete;
    MoonlightControlChannel& operator=(const MoonlightControlChannel&) = delete;

    // Handlers fire on the internal receive thread. Install them before connect.
    using RumbleHandler = std::function<void(const moonlight::RumbleEvent&)>;
    using RumbleTriggerHandler = std::function<void(const moonlight::RumbleTriggerEvent&)>;
    using MotionRequestHandler = std::function<void(const moonlight::MotionRequestEvent&)>;
    using RgbLedHandler = std::function<void(const moonlight::RgbLedEvent&)>;
    // Called when the host disconnects or sends TERMINATION.
    using DisconnectHandler = std::function<void()>;

    void setRumbleHandler(RumbleHandler h) { rumbleHandler_ = std::move(h); }
    void setRumbleTriggerHandler(RumbleTriggerHandler h) { triggerHandler_ = std::move(h); }
    void setMotionRequestHandler(MotionRequestHandler h) { motionHandler_ = std::move(h); }
    void setRgbLedHandler(RgbLedHandler h) { ledHandler_ = std::move(h); }
    void setDisconnectHandler(DisconnectHandler h) { disconnectHandler_ = std::move(h); }

    // The 16-byte AES-GCM key is the launch rikey; `connectData` is the ENet
    // connect secret from the control SETUP response (X-SS-Connect-Data), or 0.
    // Blocks up to `timeoutMs` for the ENet CONNECT handshake. Returns false on
    // failure, leaving the channel closed.
    bool connect(const std::string& hostIp, std::uint16_t port,
                 const std::array<std::uint8_t, 16>& rikey, std::uint32_t connectData,
                 int timeoutMs = 2000);

    // TERMINATION (best effort) + ENet disconnect + receive-thread join.
    void disconnect();

    bool isConnected() const { return connected_.load(std::memory_order_relaxed); }

    // Hot path: called from the SDL input thread. Encodes + seals + sends one
    // CONTROLLER_MULTI. No heap allocation in our layer.
    void sendControllerState(const moonlight::ControllerState& state);

    // Cold-path control messages (arrival, motion forward, ping, termination).
    void sendControllerArrival(std::uint8_t controllerNumber, std::uint8_t controllerType,
                               std::uint8_t capabilities, std::uint32_t supportedButtons);
    void sendControllerMotion(std::uint8_t controllerNumber, std::uint8_t motionType, float x,
                              float y, float z);
    void sendPeriodicPing();

  private:
    // Seals `plaintext` under the next seq and reliably sends it. Serialised by
    // sendMtx_ because the seq counter and the ENet host are single-writer.
    void sealAndSend(const std::uint8_t* plaintext, std::size_t len);
    void receiveLoop();

    _ENetHost* host_ = nullptr;
    _ENetPeer* peer_ = nullptr;

    std::mutex sendMtx_;
    std::unique_ptr<moonlight::crypto::ControlSealer> sealer_;
    std::uint32_t seq_ = 0;
    // Reused plaintext scratch for the hot-path encoder (never per-packet heap).
    std::array<std::uint8_t, moonlight::kControllerMultiBytes> multiScratch_{};

    std::thread rxThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};

    RumbleHandler rumbleHandler_;
    RumbleTriggerHandler triggerHandler_;
    MotionRequestHandler motionHandler_;
    RgbLedHandler ledHandler_;
    DisconnectHandler disconnectHandler_;

    std::array<std::uint8_t, 16> key_{};
    bool enetRef_ = false;
};

} // namespace dish::net
