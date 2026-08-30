// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/MoonlightControlChannel.h"

#include <enet/enet.h>

#include <chrono>
#include <cstring>
#include <mutex>

namespace dish::net {

namespace {

// One process-wide ENet init, reference-counted so overlapping channels share it.
std::mutex g_enetMtx;
int g_enetRefs = 0;

} // namespace

void moonlightEnetRef() {
    std::lock_guard<std::mutex> lock(g_enetMtx);
    if (g_enetRefs == 0) { enet_initialize(); }
    ++g_enetRefs;
}

void moonlightEnetUnref() {
    std::lock_guard<std::mutex> lock(g_enetMtx);
    if (g_enetRefs > 0) {
        --g_enetRefs;
        if (g_enetRefs == 0) { enet_deinitialize(); }
    }
}

MoonlightControlChannel::MoonlightControlChannel() {
    moonlightEnetRef();
    enetRef_ = true;
}

MoonlightControlChannel::~MoonlightControlChannel() {
    disconnect();
    if (enetRef_) { moonlightEnetUnref(); }
}

bool MoonlightControlChannel::connect(const std::string& hostIp, std::uint16_t port,
                                      const std::array<std::uint8_t, 16>& rikey,
                                      std::uint32_t connectData, int timeoutMs) {
    disconnect();

    {
        // The key and the sealer are read under sendMtx_ by every sender, and
        // the cancel flag is what a disconnect() landing mid-handshake raises.
        std::lock_guard<std::mutex> lock(sendMtx_);
        key_ = rikey;
        terminated_ = false;
        cancelled_ = false;
        sealer_ = std::make_unique<moonlight::crypto::ControlSealer>(key_);
        if (!sealer_->ok()) {
            sealer_.reset();
            return false;
        }
        seq_ = 0;
    }

    // LOCALS UNTIL THE LINK IS UP. host_ and peer_ are what disconnect() destroys
    // and what every sender reads, and this call blocks in the handshake below
    // with no lock it could hold for that long, so nothing is published until
    // the host has answered. A disconnect() that lands meanwhile finds nothing
    // to destroy and leaves the flag; the link it would have cut is torn down
    // here instead of outliving it.
    //
    // A client host (no bound address): 1 outgoing peer, 1 channel is enough for
    // the control stream. The cgutman fork takes the address family first and a
    // sockaddr_storage-based ENetAddress, so the port is set via the setter.
    ENetHost* host = enet_host_create(AF_INET, nullptr, 1, 1, 0, 0);
    if (host == nullptr) { return false; }

    ENetAddress address{};
    if (enet_address_set_host(&address, hostIp.c_str()) != 0) {
        enet_host_destroy(host);
        return false;
    }
    enet_address_set_port(&address, port);

    // The connect `data` word carries the ENet secret the host handed us in the
    // control SETUP response, so it can match this peer to the launched session.
    ENetPeer* peer = enet_host_connect(host, &address, 1, connectData);
    if (peer == nullptr) {
        enet_host_destroy(host);
        return false;
    }

    ENetEvent event;
    const bool up =
        enet_host_service(host, &event, timeoutMs) > 0 && event.type == ENET_EVENT_TYPE_CONNECT;
    if (up) {
        std::lock_guard<std::mutex> lock(sendMtx_);
        if (!cancelled_) {
            host_ = host;
            peer_ = peer;
            // Pairs with the acquire in sealAndSend: a sender that sees the link
            // up sees the host and peer it was published with.
            connected_.store(true, std::memory_order_release);
            running_.store(true, std::memory_order_relaxed);
            rxThread_ = std::thread([this] { receiveLoop(); });
            return true;
        }
    }

    if (up) {
        enet_peer_disconnect_now(peer, 0);
    } else {
        enet_peer_reset(peer);
    }
    enet_host_destroy(host);
    std::lock_guard<std::mutex> lock(sendMtx_);
    sealer_.reset();
    return false;
}

void MoonlightControlChannel::disconnect() {
    {
        // A connect() blocked in its handshake cannot be interrupted; the flag
        // makes it tear down whatever it establishes instead of publishing it.
        std::lock_guard<std::mutex> lock(sendMtx_);
        cancelled_ = true;
    }
    if (running_.exchange(false, std::memory_order_relaxed)) {
        // Best-effort graceful TERMINATION before we drop the link.
        if (connected_.load(std::memory_order_relaxed)) {
            const auto term = moonlight::encodeTermination();
            sealAndSend(term.data(), term.size());
        }
    }
    if (rxThread_.joinable()) { rxThread_.join(); }

    std::lock_guard<std::mutex> lock(sendMtx_);
    if (peer_ != nullptr) {
        enet_peer_disconnect_now(peer_, 0);
        peer_ = nullptr;
    }
    if (host_ != nullptr) {
        enet_host_destroy(host_);
        host_ = nullptr;
    }
    connected_.store(false, std::memory_order_relaxed);
    sealer_.reset();
}

void MoonlightControlChannel::sealAndSend(const std::uint8_t* plaintext, std::size_t len) {
    // Before the lock: a keepalive tick during the handshake must not wait on
    // it, and until the link is published there is nothing here to send on.
    if (!connected_.load(std::memory_order_acquire)) { return; }
    std::lock_guard<std::mutex> lock(sendMtx_);
    if (host_ == nullptr || peer_ == nullptr || !sealer_) { return; }
    std::size_t outLen = 0;
    const std::uint8_t* pkt = sealer_->seal(seq_, plaintext, len, &outLen);
    if (pkt == nullptr) { return; }
    ++seq_;
    // ENet copies our buffer into its own packet (RELIABLE, ordered).
    ENetPacket* packet = enet_packet_create(pkt, outLen, ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) { return; }
    if (enet_peer_send(peer_, 0, packet) < 0) {
        enet_packet_destroy(packet);
        return;
    }
    enet_host_flush(host_);
}

void MoonlightControlChannel::sendControllerState(const moonlight::ControllerState& state) {
    // Hot path: encode into the reused scratch, then seal+send. No allocation
    // in our code; the seq counter and host are serialised by sendMtx_.
    std::lock_guard<std::mutex> lock(sendMtx_);
    if (host_ == nullptr || peer_ == nullptr || !sealer_) { return; }
    const std::size_t n = moonlight::encodeControllerMulti(state, multiScratch_.data());
    std::size_t outLen = 0;
    const std::uint8_t* pkt = sealer_->seal(seq_, multiScratch_.data(), n, &outLen);
    if (pkt == nullptr) { return; }
    ++seq_;
    ENetPacket* packet = enet_packet_create(pkt, outLen, ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) { return; }
    if (enet_peer_send(peer_, 0, packet) < 0) {
        enet_packet_destroy(packet);
        return;
    }
    enet_host_flush(host_);
}

void MoonlightControlChannel::sendControllerArrival(std::uint8_t controllerNumber,
                                                    std::uint8_t controllerType,
                                                    std::uint8_t capabilities,
                                                    std::uint32_t supportedButtons) {
    const auto p = moonlight::encodeControllerArrival(controllerNumber, controllerType,
                                                      capabilities, supportedButtons);
    sealAndSend(p.data(), p.size());
}

void MoonlightControlChannel::sendControllerMotion(std::uint8_t controllerNumber,
                                                   std::uint8_t motionType, float x, float y,
                                                   float z) {
    const auto p = moonlight::encodeControllerMotion(controllerNumber, motionType, x, y, z);
    sealAndSend(p.data(), p.size());
}

void MoonlightControlChannel::sendControllerBattery(std::uint8_t controllerNumber,
                                                    std::uint8_t batteryState,
                                                    std::uint8_t percentage) {
    const auto p = moonlight::encodeControllerBattery(controllerNumber, batteryState, percentage);
    sealAndSend(p.data(), p.size());
}

void MoonlightControlChannel::sendPeriodicPing() {
    const auto p = moonlight::encodePeriodicPing();
    sealAndSend(p.data(), p.size());
}

void MoonlightControlChannel::receiveLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        ENetEvent event;
        int rc = 0;
        {
            // Serviced without waiting: this lock is the one every hot-path send
            // takes, and a 16 ms wait held inside it was 16 ms a controller
            // report could queue behind the receive side.
            std::lock_guard<std::mutex> lock(sendMtx_);
            if (host_ == nullptr) { break; }
            rc = enet_host_service(host_, &event, 0);
        }
        if (rc <= 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            continue;
        }

        if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
            connected_.store(false, std::memory_order_relaxed);
            if (disconnectHandler_) { disconnectHandler_(terminated_); }
            break;
        }
        if (event.type != ENET_EVENT_TYPE_RECEIVE) { continue; }

        const auto opened =
            moonlight::crypto::openControl(key_, event.packet->data, event.packet->dataLength);
        if (opened.has_value()) {
            const auto ev = moonlight::decodeServerEvent(opened->data(), opened->size());
            if (ev.has_value()) {
                switch (ev->type) {
                case moonlight::ServerEventType::Rumble:
                    if (rumbleHandler_) { rumbleHandler_(ev->rumble); }
                    break;
                case moonlight::ServerEventType::RumbleTriggers:
                    if (triggerHandler_) { triggerHandler_(ev->triggers); }
                    break;
                case moonlight::ServerEventType::MotionEvent:
                    if (motionHandler_) { motionHandler_(ev->motion); }
                    break;
                case moonlight::ServerEventType::RgbLed:
                    if (ledHandler_) { ledHandler_(ev->led); }
                    break;
                case moonlight::ServerEventType::Termination:
                    terminated_ = true;
                    break;
                case moonlight::ServerEventType::Unknown:
                    // Ignored gracefully.
                    break;
                }
            }
        }
        enet_packet_destroy(event.packet);
    }
}

} // namespace dish::net
