// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The control half of a Moonlight host on loopback: a real ENet listener, on a
// thread of its own, that opens every packet a client seals with the session
// key and records what it heard.
//
// ITS OWN THREAD IS THE POINT. Whether a client keeps talking while the thread
// that owns its session is busy can only be seen by something that is not that
// thread, which is exactly what a host is. Everything it records is an atomic,
// written here and read by the case on its own thread; what the case asks it to
// send waits in an outbox until that thread sends it.
//
// It binds 127.0.0.1 on an ephemeral port, so a run reaches no machine but this
// one, and never a Sunshine this machine may itself be running.

#pragma once

#include "Network/MoonlightControlChannel.h"
#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightCrypto.h"

#include <enet/enet.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace dish::test {

class MoonlightFakeControlHost {
  public:
    explicit MoonlightFakeControlHost(const std::array<std::uint8_t, 16>& sessionKey)
        : key_(sessionKey) {
        net::moonlightEnetRef();
        ENetAddress address{};
        if (enet_address_set_host(&address, "127.0.0.1") != 0) { return; }
        enet_address_set_port(&address, 0);
        host_ = enet_host_create(AF_INET, &address, 1, 1, 0, 0);
        if (host_ == nullptr) { return; }
        port_ = boundPort(*host_);
        running_.store(true, std::memory_order_relaxed);
        thread_ = std::thread(&MoonlightFakeControlHost::serve, this);
    }

    ~MoonlightFakeControlHost() {
        running_.store(false, std::memory_order_relaxed);
        if (thread_.joinable()) { thread_.join(); }
        if (host_ != nullptr) { enet_host_destroy(host_); }
        net::moonlightEnetUnref();
    }

    MoonlightFakeControlHost(const MoonlightFakeControlHost&) = delete;
    MoonlightFakeControlHost& operator=(const MoonlightFakeControlHost&) = delete;
    MoonlightFakeControlHost(MoonlightFakeControlHost&&) = delete;
    MoonlightFakeControlHost& operator=(MoonlightFakeControlHost&&) = delete;

    bool listening() const { return port_ != 0; }
    std::uint16_t port() const { return port_; }

    // PERIODIC_PINGs that arrived sealed under the session key.
    int keepalives() const { return keepalives_.load(std::memory_order_relaxed); }

    // INPUT_DATA packets (controller frames, arrivals, motion, touch) that arrived sealed under the
    // session key.
    int inputs() const { return inputs_.load(std::memory_order_relaxed); }

    // The client's end of the link went away after it had come up.
    bool clientLeft() const { return clientLeft_.load(std::memory_order_relaxed); }

    // Sealed under the session key and sent reliably, the way a host sends its control events,
    // once the client is connected on the host's side.
    void send(std::vector<std::uint8_t> plaintext) {
        const std::lock_guard<std::mutex> lock(outboxMtx_);
        outbox_.push_back(std::move(plaintext));
    }

    // The host ends the link, after everything sent before it has been acknowledged.
    void hangUp() {
        const std::lock_guard<std::mutex> lock(outboxMtx_);
        hangUpAsked_ = true;
    }

    // The client's next datagram is read off the socket before ENet sees it, the way a lossy link
    // drops one. losing() is true while the host waits for that datagram.
    void loseNextDatagram() { loseNext_.store(true, std::memory_order_relaxed); }
    bool losing() const { return losing_.load(std::memory_order_relaxed); }
    int datagramsLost() const { return lost_.load(std::memory_order_relaxed); }

  private:
    // ENet has no port getter; the bound port is only knowable from the address
    // the bind filled in.
    static std::uint16_t boundPort(const ENetHost& host) {
        const auto* bound = reinterpret_cast<const sockaddr_in*>(&host.address.address);
        return ntohs(bound->sin_port);
    }

    void serve() {
        while (running_.load(std::memory_order_relaxed)) {
            sendOutbox();
            if (loseNext_.load(std::memory_order_relaxed)) {
                loseOne();
                continue;
            }
            ENetEvent event{};
            if (enet_host_service(host_, &event, 5) <= 0) { continue; }
            switch (event.type) {
            case ENET_EVENT_TYPE_RECEIVE:
                absorb(*event.packet);
                enet_packet_destroy(event.packet);
                break;
            case ENET_EVENT_TYPE_DISCONNECT:
                peer_ = nullptr;
                clientLeft_.store(true, std::memory_order_relaxed);
                break;
            case ENET_EVENT_TYPE_CONNECT:
                peer_ = event.peer;
                break;
            case ENET_EVENT_TYPE_NONE:
                break;
            }
        }
    }

    void absorb(const ENetPacket& packet) {
        const auto opened = moonlight::crypto::openControl(key_, packet.data, packet.dataLength);
        if (!opened.has_value() || opened->size() < 2) { return; }
        const auto type = static_cast<std::uint16_t>((*opened)[0] | ((*opened)[1] << 8));
        if (type == moonlight::kCtrlPeriodicPing) {
            keepalives_.fetch_add(1, std::memory_order_relaxed);
        }
        if (type == moonlight::kCtrlInputData) { inputs_.fetch_add(1, std::memory_order_relaxed); }
    }

    void sendOutbox() {
        if (peer_ == nullptr) { return; }
        std::vector<std::vector<std::uint8_t>> plaintexts;
        bool hangUp = false;
        {
            const std::lock_guard<std::mutex> lock(outboxMtx_);
            plaintexts.swap(outbox_);
            hangUp = std::exchange(hangUpAsked_, false);
        }
        for (const auto& plaintext : plaintexts) { sendSealed(plaintext); }
        if (hangUp) { enet_peer_disconnect_later(peer_, 0); }
    }

    void sendSealed(const std::vector<std::uint8_t>& plaintext) {
        const auto sealed =
            moonlight::crypto::sealControl(key_, hostSeq_++, plaintext.data(), plaintext.size());
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

    // The flush first: an acknowledgement this host still owes would otherwise go unsent, and the
    // client's resend of what it acknowledges would be the datagram lost instead.
    void loseOne() {
        enet_host_flush(host_);
        losing_.store(true, std::memory_order_relaxed);
        enet_uint32 condition = ENET_SOCKET_WAIT_RECEIVE;
        if (enet_socket_wait(host_->socket, &condition, 5) != 0) { return; }
        if ((condition & ENET_SOCKET_WAIT_RECEIVE) == 0) { return; }
        std::array<std::uint8_t, ENET_PROTOCOL_MAXIMUM_MTU> datagram{};
        ENetBuffer buffer{};
        buffer.data = datagram.data();
        buffer.dataLength = datagram.size();
        ENetAddress from{};
        if (enet_socket_receive(host_->socket, &from, nullptr, &buffer, 1) <= 0) { return; }
        loseNext_.store(false, std::memory_order_relaxed);
        losing_.store(false, std::memory_order_relaxed);
        lost_.fetch_add(1, std::memory_order_relaxed);
    }

    std::array<std::uint8_t, 16> key_;
    ENetHost* host_ = nullptr;
    std::uint16_t port_ = 0;
    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<int> keepalives_{0};
    std::atomic<int> inputs_{0};
    std::atomic<bool> clientLeft_{false};
    std::atomic<bool> loseNext_{false};
    std::atomic<bool> losing_{false};
    std::atomic<int> lost_{0};

    // Host thread only.
    ENetPeer* peer_ = nullptr;
    std::uint32_t hostSeq_ = 0;

    // Filled by the case, emptied by the host thread.
    std::mutex outboxMtx_;
    std::vector<std::vector<std::uint8_t>> outbox_;
    bool hangUpAsked_ = false;
};

} // namespace dish::test
