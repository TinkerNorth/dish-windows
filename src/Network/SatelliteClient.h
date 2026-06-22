// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Encrypted UDP data-plane session to a single Satellite server (protocol-1).
// The Windows analogue of dish-android's satellite_jni UDP path. Owns one raw
// Winsock UDP socket, the per-session ChaCha20-Poly1305 key/token, a monotonic
// per-direction nonce counter (starting at 1), a heartbeat sender thread and a
// receive thread.
//
// Topology is REST-only now: this class carries STREAMS only (input, heartbeat,
// motion, battery, touchpad up; heartbeat-ack, rumble, lightbar, session-close
// down). The deleted opcodes 0x0004/5/6/7/8/E and the UDP controller-
// registration FSM are gone.
//
// Crypto is delegated to core/wire/SessionCrypto (frozen, byte-exact vs the
// satellite): nonce = dir(1)|0x00×7|counter(4 BE), AAD = token(4 BE), key = the
// HKDF-derived sessionKey (NEVER the raw pairing key). See contract §Crypto.
//
// Thread-safety:
//   * sendReport() is called directly from the SDL gamepad thread for minimum
//     latency and is lock-free except for the duration of one ::sendto.
//   * Public lifecycle calls (open/close, setConnectionParams, start/stop) are
//     expected to be invoked from a single owner thread (typically Qt main).

#pragma once

#include "core/model/Protocol.h"

// Winsock headers must appear before any windows.h pull-in that could include
// the older winsock.h. <ws2tcpip.h> declares sockaddr_in / inet_pton; pulled
// in here so the header surface stays self-contained.
#include <winsock2.h>
#include <ws2tcpip.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace dish::net {

class SatelliteClient {
  public:
    // Opcodes mirror core/model/Protocol.h (the single Qt-free source). Kept as
    // members so existing call sites and tests can reference them by class.
    static constexpr std::uint16_t kMsgInput = proto::kMsgInput;
    static constexpr std::uint16_t kMsgHeartbeatPing = proto::kMsgHeartbeat;
    static constexpr std::uint16_t kMsgHeartbeatAck = proto::kMsgHeartbeatAck;
    static constexpr std::uint16_t kMsgRumble = proto::kMsgRumble;
    static constexpr std::uint16_t kMsgMotion = proto::kMsgMotion;
    static constexpr std::uint16_t kMsgBattery = proto::kMsgBattery;
    static constexpr std::uint16_t kMsgTouchpad = proto::kMsgTouchpad;
    static constexpr std::uint16_t kMsgLightbar = proto::kMsgLightbar;
    static constexpr std::uint16_t kMsgSessionClose = proto::kMsgSessionClose;

    // Controller capability bits (carried in the REST descriptor caps object;
    // kept here too so SDL-bound capability folding stays unit-testable).
    static constexpr std::uint16_t kCapAnalogTriggers = proto::kCapAnalogTriggers;
    static constexpr std::uint16_t kCapRumble = proto::kCapRumble;
    static constexpr std::uint16_t kCapMotion = proto::kCapMotion;
    static constexpr std::uint16_t kCapLightbar = proto::kCapLightbar;

    // Battery wire constants (satellite/src/core/types.h mirrors).
    static constexpr std::uint8_t kBatteryLevelUnknown = 0xFF;
    static constexpr std::uint8_t kBatteryStatusUnknown = 0;
    static constexpr std::uint8_t kBatteryStatusDischarging = 1;
    static constexpr std::uint8_t kBatteryStatusCharging = 2;
    static constexpr std::uint8_t kBatteryStatusFull = 3;
    static constexpr std::uint8_t kBatteryStatusWired = 4;

    // Heartbeat cadence is 2000 ms; "not responding" at 2 misses, dead at 5
    // (contract §Liveness — verified against satellite types.h
    // HEARTBEAT_INTERVAL_SEC=2 / HEARTBEAT_MISS_MAX=5).
    static constexpr std::uint32_t kHeartbeatIntervalMs = 2000;
    static constexpr int kHeartbeatMissNotResponding = 2;
    static constexpr int kHeartbeatMissMax = 5;

    SatelliteClient();
    ~SatelliteClient();

    SatelliteClient(const SatelliteClient&) = delete;
    SatelliteClient& operator=(const SatelliteClient&) = delete;
    SatelliteClient(SatelliteClient&&) = delete;
    SatelliteClient& operator=(SatelliteClient&&) = delete;

    // Returns true on success. On failure the client is left closed.
    bool openSocket(const std::string& ip, int port);
    void closeSocket();

    // Install the post-PUT token (4B) + the HKDF-derived 32B sessionKey. Resets
    // the send counter to 1 and the recv replay guard. Call after each session
    // PUT (token/salt/key rotate; counters restart at 1 with no nonce reuse).
    void setConnectionParams(const std::array<std::uint8_t, 4>& token,
                             const std::array<std::uint8_t, 32>& key);

    // Hot path: called directly from the SDL input thread.
    void sendReport(int controllerIndex, std::uint16_t buttons, std::uint8_t lt, std::uint8_t rt,
                    std::int16_t lx, std::int16_t ly, std::int16_t rx, std::int16_t ry);

    // Pure helper: fold the per-controller CAP_LIGHTBAR (0x0008) bit into a base
    // capability word. Public + static so the cap-advertisement rule is
    // unit-testable without a live socket.
    static std::uint16_t withLightbarCapability(std::uint16_t base, bool hasLightbar) {
        return static_cast<std::uint16_t>(base | (hasLightbar ? kCapLightbar : 0));
    }

    // Pure helper: fold the per-controller CAP_MOTION (0x0004) bit into a base
    // capability word — an Xbox pad therefore never advertises CAP_MOTION.
    static std::uint16_t withMotionCapability(std::uint16_t base, bool hasMotion) {
        return static_cast<std::uint16_t>(base | (hasMotion ? kCapMotion : 0));
    }

    // Forward a single IMU sample (MSG_MOTION 0x000A). Axes follow the
    // satellite's right-handed convention (+X right, +Y up, +Z toward player);
    // SDL applies the manufacturer rotation for HIDAPI controllers, so samples
    // reaching here are already in the satellite frame. Scale: gyro int16 LSB =
    // 2000/32767 deg/s; accel int16 LSB = 4/32767 g. `timestampDeltaUs` is µs
    // since the previous motion packet for this controller (0 on the first).
    void sendMotion(int controllerIndex, std::int16_t gyroX, std::int16_t gyroY, std::int16_t gyroZ,
                    std::int16_t accelX, std::int16_t accelY, std::int16_t accelZ,
                    std::uint32_t timestampDeltaUs);

    // Forward a single battery sample (MSG_BATTERY 0x000B). `level` is 0..100 or
    // kBatteryLevelUnknown (0xFF); `status` is a kBatteryStatus* constant.
    void sendBattery(int controllerIndex, std::uint8_t level, std::uint8_t status);

    // Forward a touchpad sample (MSG_TOUCHPAD 0x000C — DualSense / DS4). Up to
    // two fingers + the clickable-pad switch. `eventTimeMs` is the sender-side
    // sample timestamp (uptime ms, u32) — protocol-1 requires it (mouse-mode
    // timing depends on it). Coordinates are normalised int16.
    void sendTouchpad(int controllerIndex, bool finger0Active, std::uint8_t finger0Id,
                      std::int16_t finger0X, std::int16_t finger0Y, bool finger1Active,
                      std::uint8_t finger1Id, std::int16_t finger1X, std::int16_t finger1Y,
                      bool buttonPressed, std::uint32_t eventTimeMs);

    // Pure encoder for the MSG_MOTION inner payload (after the 4-byte
    // type+length header). Host-LE int16/uint32, matching the satellite's
    // decodeMotionReport. 17 bytes: ctrlIdx + 6×i16 + u32.
    static std::array<std::uint8_t, 17>
    encodeMotionPayload(std::uint8_t controllerIndex, std::int16_t gyroX, std::int16_t gyroY,
                        std::int16_t gyroZ, std::int16_t accelX, std::int16_t accelY,
                        std::int16_t accelZ, std::uint32_t timestampDeltaUs);

    // Pure encoder for the MSG_BATTERY inner payload: ctrlIdx + level + status.
    static std::array<std::uint8_t, 3>
    encodeBatteryPayload(std::uint8_t controllerIndex, std::uint8_t level, std::uint8_t status);

    // Pure encoder for the MSG_TOUCHPAD inner payload. 16 bytes:
    // ctrlIdx(1) + flags(1) + f0(id1 + x2 + y2) + f1(id1 + x2 + y2) +
    // eventTimeMs(u32 LE). `flags` bit0=finger0 active, bit1=finger1 active,
    // bit2=button. Coordinates + eventTimeMs are host-LE. The 15-byte
    // post-ctrlIdx body matches satellite types.h::TouchpadReport (protocol-1).
    static std::array<std::uint8_t, 16>
    encodeTouchpadPayload(std::uint8_t controllerIndex, bool finger0Active, std::uint8_t finger0Id,
                          std::int16_t finger0X, std::int16_t finger0Y, bool finger1Active,
                          std::uint8_t finger1Id, std::int16_t finger1X, std::int16_t finger1Y,
                          bool buttonPressed, std::uint32_t eventTimeMs);

    // Decoded rumble message from the satellite — motors + duration only.
    struct RumbleMessage {
        int controllerIndex = 0;
        std::uint16_t strongMagnitude = 0;
        std::uint16_t weakMagnitude = 0;
        std::uint16_t durationMs = 0;
    };

    using RumbleHandler = std::function<void(const RumbleMessage&)>;
    void setRumbleHandler(RumbleHandler handler);

    // Length of the MSG_RUMBLE inner payload (after the 4-byte type+length
    // header): ctrlIdx(1) + strong(2) + weak(2) + durMs(2).
    static constexpr std::size_t kRumblePayloadLen = 7;

    // Pure decoder for the MSG_RUMBLE inner payload (header already stripped).
    // Wire layout (fixed 7 bytes): ctrlIdx(1) strong(2 BE) weak(2 BE) durMs(2 BE).
    static std::optional<RumbleMessage> parseRumbleMessage(const std::uint8_t* payload,
                                                           std::size_t len);

    // Decoded lightbar message from the satellite (dedicated colour stream).
    struct LightbarMessage {
        int controllerIndex = 0;
        std::uint8_t r = 0;
        std::uint8_t g = 0;
        std::uint8_t b = 0;
    };

    using LightbarHandler = std::function<void(const LightbarMessage&)>;
    void setLightbarHandler(LightbarHandler handler);

    // Pure decoder for the MSG_LIGHTBAR inner payload: ctrlIdx + r + g + b = 4.
    static std::optional<LightbarMessage> parseLightbarMessage(const std::uint8_t* payload,
                                                               std::size_t len);

    // Decoded enriched heartbeat ack (MSG_HEARTBEAT_ACK 0x0003).
    // backendAvailable(1) + totalActiveControllers(1) + epoch(u16 BE) +
    // activeBitmap(u16 BE). Drives the reconcile loop.
    struct HeartbeatAck {
        bool backendAvailable = false;
        std::uint8_t totalActiveControllers = 0;
        std::uint16_t epoch = 0;
        std::uint16_t activeBitmap = 0;
    };

    // Pure decoder for the MSG_HEARTBEAT_ACK inner payload (header stripped).
    // std::nullopt when shorter than kHeartbeatAckPayloadBytes (a bare ack from
    // a pre-protocol-1 server is then ignored for reconcile, only liveness
    // counts). Public + static so the parse is unit-testable.
    static std::optional<HeartbeatAck> parseHeartbeatAck(const std::uint8_t* payload,
                                                         std::size_t len);

    // Install a handler invoked from the receive thread on every enriched ack.
    // The session uses it to drive the epoch/bitmap reconcile.
    using HeartbeatAckHandler = std::function<void(const HeartbeatAck&)>;
    void setHeartbeatAckHandler(HeartbeatAckHandler handler);

    // Install a handler invoked from the receive thread on a SESSION_CLOSE
    // (0x000F). The argument is the reason byte (proto::kCloseReason*). The
    // session maps it to a teardown action (drop-key / stay-down / retry).
    using CloseHandler = std::function<void(std::uint8_t reason)>;
    void setCloseHandler(CloseHandler handler);

    void startHeartbeat();
    void stopHeartbeat();
    void startReceiveLoop();
    void stopReceiveLoop();

    bool isOpen() const { return sock_ != INVALID_SOCKET; }
    bool isAlive() const { return connectionAlive_.load(std::memory_order_relaxed); }
    int missedAcks() const { return missedAcks_.load(std::memory_order_relaxed); }
    // Last enriched-ack values, surfaced for the reconcile poll (the session can
    // either subscribe via the handler or poll these atomics). -1 until the
    // first enriched ack arrives.
    std::int32_t serverEpoch() const { return serverEpoch_.load(std::memory_order_relaxed); }
    std::int32_t serverBitmap() const { return serverBitmap_.load(std::memory_order_relaxed); }
    std::int8_t backendAvailable() const {
        return backendAvailable_.load(std::memory_order_relaxed);
    }
    std::int8_t activeControllerCount() const {
        return activeControllerCount_.load(std::memory_order_relaxed);
    }
    // The reason byte of a received SESSION_CLOSE, or -1 if none. The session's
    // alive-poll can treat a non-negative value as terminal-now (no death wait).
    std::int32_t sessionCloseReason() const {
        return sessionCloseReason_.load(std::memory_order_relaxed);
    }
    // Current send counter (next value to use), for the proactive-re-PUT guard.
    std::uint32_t sendCounter() const { return sendCounter_.load(std::memory_order_relaxed); }

  private:
    void sendEncrypted(std::uint16_t msgType, const std::uint8_t* payload, std::size_t len);
    void heartbeatLoop();
    void receiveLoop();
    void processIncoming(const std::uint8_t* buf, std::size_t n);

    SOCKET sock_ = INVALID_SOCKET;
    sockaddr_in dest_{};
    std::array<std::uint8_t, 4> token_{};
    std::uint32_t tokenBe_ = 0; // token as a host u32 (the 4 raw BE bytes), for AAD
    std::array<std::uint8_t, 32> key_{};
    // Per-direction send counter, starting at 1 (contract §Crypto). Plain
    // atomic increment; the send lock serialises the ::sendto, not this.
    std::atomic<std::uint32_t> sendCounter_{1};
    // Receiver replay guard (server→client direction): drop counter <=
    // lastRecvCounter_ (first packet exempt while it is 0).
    std::uint32_t lastRecvCounter_ = 0;
    std::mutex sendLock_;

    std::atomic<bool> heartbeatRunning_{false};
    std::atomic<bool> ackRunning_{false};
    std::thread heartbeatThread_;
    std::thread ackThread_;

    std::atomic<int> missedAcks_{0};
    std::atomic<bool> connectionAlive_{true};
    std::atomic<std::int32_t> serverEpoch_{-1};
    std::atomic<std::int32_t> serverBitmap_{-1};
    std::atomic<std::int8_t> backendAvailable_{-1};
    std::atomic<std::int8_t> activeControllerCount_{-1};
    std::atomic<std::int32_t> sessionCloseReason_{-1};

    std::mutex rumbleHandlerMtx_;
    RumbleHandler rumbleHandler_;
    std::mutex lightbarHandlerMtx_;
    LightbarHandler lightbarHandler_;
    std::mutex ackHandlerMtx_;
    HeartbeatAckHandler ackHandler_;
    std::mutex closeHandlerMtx_;
    CloseHandler closeHandler_;
};

} // namespace dish::net
