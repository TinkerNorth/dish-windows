// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The far end of a Moonlight control stream, on loopback, keeping the joypad table a Wolf host
// keeps for one session.
//
// Which pad the host ends up holding under a number is only visible from here: the client's own
// state says what it meant to send. These rules decide it, read from Wolf's
// src/moonlight-server/control/input_handler.cpp in the TinkerNorth fork:
//   * a CONTROLLER_ARRIVAL for a number the session already holds is skipped;
//   * a CONTROLLER_MULTI naming a held number with its bit cleared unplugs that pad;
//   * a CONTROLLER_MULTI naming a number the session does not hold plugs a default Xbox pad
//     there, whatever the mask says;
//   * a PlayStation pad that arrives with ACCELEROMETER or GYRO is asked for that motion, at
//     100 Hz, and no other pad is asked for any.
//
// It binds 127.0.0.1 on an ephemeral port and opens every packet with the key the case hands it,
// which is the rikey a host would have been given in /launch.

#pragma once

#include "Network/MoonlightControlChannel.h"
#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightCrypto.h"
#include "core/moonlight/MoonlightPadSlots.h"

#include <enet/enet.h>

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace dish::test {

class MoonlightWolfControlHost {
  public:
    using Key = std::array<std::uint8_t, moonlight::crypto::kAesKey128>;

    // One packet as the host read it. The number, mask, type, capabilities and touch event are
    // filled only for the input kinds that carry them.
    struct Packet {
        std::uint32_t seq = 0;
        std::uint32_t inputType = 0;
        std::uint16_t number = 0;
        std::uint16_t mask = 0;
        std::uint8_t type = 0;
        std::uint8_t capabilities = 0;
        std::uint8_t touchEvent = 0;
    };

    explicit MoonlightWolfControlHost(const Key& key) : key_(key) {
        net::moonlightEnetRef();
        ENetAddress address{};
        if (enet_address_set_host(&address, "127.0.0.1") != 0) { return; }
        enet_address_set_port(&address, 0);
        host_ = enet_host_create(AF_INET, &address, 4, 1, 0, 0);
        if (host_ == nullptr) { return; }
        // ENet has no port getter: the bound port is in the address it filled in after the bind.
        const auto* bound = reinterpret_cast<const sockaddr_in*>(&host_->address.address);
        port_ = ntohs(bound->sin_port);
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread([this] { serve(); });
    }

    ~MoonlightWolfControlHost() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) { thread_.join(); }
        if (host_ != nullptr) { enet_host_destroy(host_); }
        net::moonlightEnetUnref();
    }

    MoonlightWolfControlHost(const MoonlightWolfControlHost&) = delete;
    MoonlightWolfControlHost& operator=(const MoonlightWolfControlHost&) = delete;
    MoonlightWolfControlHost(MoonlightWolfControlHost&&) = delete;
    MoonlightWolfControlHost& operator=(MoonlightWolfControlHost&&) = delete;

    bool listening() const { return host_ != nullptr && port_ != 0; }
    std::uint16_t port() const { return port_; }

    // The arrival type of the pad the session holds under `number`, empty when it holds none.
    std::optional<std::uint8_t> padType(std::uint16_t number) const {
        std::lock_guard<std::mutex> lock(mtx_);
        const auto it = pads_.find(number);
        if (it == pads_.end()) { return std::nullopt; }
        return it->second;
    }

    // Every packet read so far, in the order it arrived.
    std::vector<Packet> packets() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return packets_;
    }

  private:
    static std::uint16_t u16At(const std::uint8_t* p) {
        return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
    }

    static std::uint32_t u32At(const std::uint8_t* p) {
        return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8) |
               (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
    }

    // The host's service loop, on its own thread: the ENet host and its peer belong to it once it
    // starts.
    void serve() {
        while (running_.load(std::memory_order_relaxed)) {
            ENetEvent event{};
            if (enet_host_service(host_, &event, 5) <= 0) { continue; }
            if (event.type == ENET_EVENT_TYPE_CONNECT) { peer_ = event.peer; }
            if (event.type != ENET_EVENT_TYPE_RECEIVE) { continue; }
            absorb(event.packet->data, event.packet->dataLength);
            enet_packet_destroy(event.packet);
        }
    }

    // The sealed packet is [0x0001][len][seq u32 LE][tag][ciphertext]; the plaintext an INPUT_DATA
    // opens to is [0x0206][len][size u32 BE][input type u32 LE][body].
    void absorb(const std::uint8_t* sealed, std::size_t len) {
        const auto opened = moonlight::crypto::openControl(key_, sealed, len);
        if (!opened.has_value()) { return; }
        Packet packet;
        packet.seq = u32At(sealed + 4);
        const bool isInput =
            opened->size() >= 12 && u16At(opened->data()) == moonlight::kCtrlInputData;
        if (isInput) { readInput(*opened, packet); }
        std::optional<std::uint8_t> built;
        {
            std::lock_guard<std::mutex> lock(mtx_);
            built = applyWolfRules(packet);
            packets_.push_back(packet);
        }
        if (built == moonlight::kPadTypePlayStation) { requestMotion(packet); }
    }

    static void readInput(const moonlight::crypto::Bytes& plaintext, Packet& packet) {
        const std::uint8_t* body = plaintext.data() + 12;
        packet.inputType = u32At(plaintext.data() + 8);
        const bool isArrival = packet.inputType == moonlight::kInputControllerArrival &&
                               plaintext.size() >= 12 + moonlight::kControllerArrivalBody;
        const bool isMulti = packet.inputType == moonlight::kInputControllerMulti &&
                             plaintext.size() >= moonlight::kControllerMultiBytes;
        const bool isTouch =
            packet.inputType == moonlight::kInputControllerTouch && plaintext.size() >= 14;
        if (isArrival) {
            packet.number = body[0];
            packet.type = body[1];
            packet.capabilities = body[2];
        }
        if (isMulti) {
            packet.number = u16At(body + 2);
            packet.mask = u16At(body + 4);
        }
        if (isTouch) {
            packet.number = body[0];
            packet.touchEvent = body[1];
        }
    }

    // mtx_ held. The type of the pad an arrival built, empty when it built none.
    std::optional<std::uint8_t> applyWolfRules(const Packet& packet) {
        if (packet.inputType == moonlight::kInputControllerArrival) { return arrive(packet); }
        if (packet.inputType == moonlight::kInputControllerMulti) { multi(packet); }
        return std::nullopt;
    }

    // create_new_joypad's pick: the type asked for, or for an Unknown one a PlayStation pad when
    // the arrival carries motion and an Xbox pad when it does not.
    static std::uint8_t builtType(const Packet& packet) {
        const bool carriesMotion = (packet.capabilities & moonlight::kCapsReadAtArrival) != 0;
        if (packet.type != moonlight::kPadTypeUnknown) { return packet.type; }
        return carriesMotion ? moonlight::kPadTypePlayStation : moonlight::kPadTypeXbox;
    }

    // controller_arrival: a number the session holds is skipped.
    std::optional<std::uint8_t> arrive(const Packet& packet) {
        if (pads_.count(packet.number) != 0) { return std::nullopt; }
        const std::uint8_t type = builtType(packet);
        pads_[packet.number] = type;
        return type;
    }

    // Service thread. One MOTION_EVENT per sensor the arrival declared, the way create_new_joypad
    // asks a PlayStation pad for them.
    void requestMotion(const Packet& arrival) {
        if (peer_ == nullptr) { return; }
        const std::pair<std::uint8_t, std::uint8_t> sensors[] = {
            {moonlight::kPadCapAccel, moonlight::kMotionAccel},
            {moonlight::kPadCapGyro, moonlight::kMotionGyro},
        };
        for (const auto& [capability, motionType] : sensors) {
            if ((arrival.capabilities & capability) == 0) { continue; }
            sendMotionEvent(arrival.number, motionType);
        }
    }

    // [0x5501][len 5][controller u16][rate u16][type u8], sealed the way the client seals.
    void sendMotionEvent(std::uint16_t number, std::uint8_t motionType) {
        const std::uint8_t plaintext[9] = {0x01,
                                           0x55,
                                           0x05,
                                           0x00,
                                           static_cast<std::uint8_t>(number & 0xFF),
                                           static_cast<std::uint8_t>(number >> 8),
                                           kMotionRateHz,
                                           0x00,
                                           motionType};
        const auto sealed =
            moonlight::crypto::sealControl(key_, hostSeq_++, plaintext, sizeof(plaintext));
        if (!sealed.has_value()) { return; }
        ENetPacket* packet =
            enet_packet_create(sealed->data(), sealed->size(), ENET_PACKET_FLAG_RELIABLE);
        if (packet == nullptr) { return; }
        if (enet_peer_send(peer_, 0, packet) < 0) {
            enet_packet_destroy(packet);
            return;
        }
        enet_host_flush(host_);
    }

    // controller_multi: a held number with its bit cleared is unplugged, and a number the session
    // does not hold gets a default Xbox pad.
    void multi(const Packet& packet) {
        const bool held = pads_.count(packet.number) != 0;
        const bool bitCleared = (packet.mask & (1U << packet.number)) == 0;
        if (held && bitCleared) {
            pads_.erase(packet.number);
            return;
        }
        if (!held) { pads_[packet.number] = moonlight::kPadTypeXbox; }
    }

    // The rate create_new_joypad asks for.
    static constexpr std::uint8_t kMotionRateHz = 100;

    Key key_;
    ENetHost* host_ = nullptr;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    // Service thread only.
    ENetPeer* peer_ = nullptr;
    std::uint32_t hostSeq_ = 0;

    // Written on the service thread, read by the case.
    mutable std::mutex mtx_;
    std::map<std::uint16_t, std::uint8_t> pads_;
    std::vector<Packet> packets_;
};

} // namespace dish::test
