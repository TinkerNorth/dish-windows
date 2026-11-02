// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What the Moonlight control link's receive thread takes the send lock for. Every controller report
// takes that lock, so the receive thread holds it for ENet's service pass alone: not while it waits
// for the host, and not while it opens and dispatches what the host sent. What the thread still
// owes the link (ENet's resends, the host's goodbye) is pinned here as well. The host is
// MoonlightFakeControlHost, on loopback and on a thread of its own.

#include "Network/MoonlightControlChannel.h"
#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightCrypto.h"

#include "MoonlightFakeControlHost.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

using dish::moonlight::ControllerState;
using dish::moonlight::RumbleEvent;
using dish::net::MoonlightControlChannel;
using dish::test::MoonlightFakeControlHost;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {

const std::array<std::uint8_t, 16> kSessionKey = {0x3C, 0x1F, 0x8A, 0x52, 0x07, 0xE4, 0x96, 0x2B,
                                                  0xD1, 0x68, 0x4E, 0xA5, 0x13, 0xF0, 0x7D, 0xC9};

constexpr int kLinkTimeoutMs = 5000;

// Every wait is bounded: a case that hangs on a host is worse than one that fails.
template <class Fn> bool waitUntil(Fn done, std::chrono::milliseconds budget = 5000ms) {
    const auto deadline = Clock::now() + budget;
    while (Clock::now() < deadline) {
        if (done()) { return true; }
        std::this_thread::sleep_for(1ms);
    }
    return done();
}

std::uint8_t lowByte(std::uint16_t v) { return static_cast<std::uint8_t>(v & 0xFFU); }
std::uint8_t highByte(std::uint16_t v) { return static_cast<std::uint8_t>(v >> 8); }

// RUMBLE_DATA as a host seals it: [0x010b][len 10][unused 4][controller][low][high], all LE.
std::vector<std::uint8_t> rumble(std::uint16_t controller, std::uint16_t low, std::uint16_t high) {
    return {0x0B,
            0x01,
            0x0A,
            0x00,
            0x00,
            0x00,
            0x00,
            0x00,
            lowByte(controller),
            highByte(controller),
            lowByte(low),
            highByte(low),
            lowByte(high),
            highByte(high)};
}

struct Span {
    Clock::time_point from;
    Clock::time_point to;

    bool contains(const Span& other) const { return other.from >= from && other.to <= to; }
};

// Appended on one thread, read on any.
class SpanLog {
  public:
    void add(Span span) {
        const std::lock_guard<std::mutex> lock(mtx_);
        spans_.push_back(span);
    }

    std::size_t size() const {
        const std::lock_guard<std::mutex> lock(mtx_);
        return spans_.size();
    }

    std::vector<Span> spans() const {
        const std::lock_guard<std::mutex> lock(mtx_);
        return spans_;
    }

  private:
    mutable std::mutex mtx_;
    std::vector<Span> spans_;
};

// Holds each packet open on the receive thread until two more reports have gone out, so the second
// started and finished inside the open, or until kOpenAtMost; logs how long it held it.
constexpr auto kOpenAtMost = 2s;

MoonlightControlChannel::PacketOpener openerAwaitingSends(SpanLog& opens, const SpanLog& sends) {
    return [&opens, &sends](const std::array<std::uint8_t, 16>& key, const std::uint8_t* packet,
                            std::size_t len) {
        const auto from = Clock::now();
        const std::size_t sentBefore = sends.size();
        while (sends.size() < sentBefore + 2 && Clock::now() - from < kOpenAtMost) {
            std::this_thread::sleep_for(1ms);
        }
        opens.add(Span{from, Clock::now()});
        return dish::moonlight::crypto::openControl(key, packet, len);
    };
}

} // namespace

TEST_CASE("A report sent while the receive thread waits for the host never waits on it",
          "[moonlight][rxlock]") {
    // A wait for the host that held the send lock would hold each report behind it for up to a
    // poll (2 ms): reports sent at scattered moments would wait about half a poll at the median,
    // which one unlucky preemption on a loaded machine cannot move.
    constexpr int kReports = 64;
    constexpr auto kMedianBound = 500us;

    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    MoonlightControlChannel link;
    REQUIRE(link.connect("127.0.0.1", host.port(), kSessionKey, 0, kLinkTimeoutMs));
    REQUIRE(waitUntil([&host] { return host.keepalives() > 0; }));

    ControllerState state;
    state.activeGamepadMask = 0x0001;
    std::vector<Clock::duration> took;
    took.reserve(kReports);
    for (int i = 0; i < kReports; ++i) {
        std::this_thread::sleep_for(1ms);
        state.buttonFlags = static_cast<std::uint16_t>(i);
        const auto start = Clock::now();
        link.sendControllerState(state);
        took.push_back(Clock::now() - start);
    }

    REQUIRE(waitUntil([&host] { return host.inputs() == kReports; }));
    const auto median = took.begin() + kReports / 2;
    std::nth_element(took.begin(), median, took.end());
    CHECK(*median < kMedianBound);
}

TEST_CASE("Reports keep going out while the receive thread opens a packet from the host",
          "[moonlight][rxlock]") {
    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    SpanLog opens;
    SpanLog sends;
    std::atomic<int> rumbles{0};
    std::atomic<std::uint32_t> lastRumble{0};
    MoonlightControlChannel link(openerAwaitingSends(opens, sends));
    link.setRumbleHandler([&rumbles, &lastRumble](const RumbleEvent& e) {
        lastRumble.store((static_cast<std::uint32_t>(e.lowFreq) << 16U) |
                         static_cast<std::uint32_t>(e.highFreq));
        rumbles.fetch_add(1);
    });
    REQUIRE(link.connect("127.0.0.1", host.port(), kSessionKey, 0, kLinkTimeoutMs));

    std::jthread sender([&link, &sends](const std::stop_token& stop) {
        ControllerState state;
        state.activeGamepadMask = 0x0001;
        while (!stop.stop_requested()) {
            const auto from = Clock::now();
            link.sendControllerState(state);
            sends.add(Span{from, Clock::now()});
            std::this_thread::sleep_for(1ms);
        }
    });
    host.send(rumble(0, 0x1234, 0xABCD));
    REQUIRE(waitUntil([&rumbles] { return rumbles.load() > 0; }));
    sender.request_stop();
    sender.join();

    const auto opened = opens.spans();
    REQUIRE(opened.size() == 1);
    const auto sent = sends.spans();
    const auto sentWhileOpen = std::count_if(
        sent.begin(), sent.end(), [&opened](const Span& s) { return opened.front().contains(s); });
    CHECK(sentWhileOpen > 0);
    CHECK(rumbles.load() == 1);
    CHECK(lastRumble.load() == 0x1234ABCDU);
}

TEST_CASE("A report the host never heard is resent long before the next keepalive",
          "[moonlight][rxlock]") {
    // ENet resends what the host has not acknowledged on its own clock, which only runs inside a
    // service pass. Nothing arrives for a lost report, and the next keepalive is half a second off.
    constexpr auto kResendBound = 250ms;

    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    MoonlightControlChannel link;
    REQUIRE(link.connect("127.0.0.1", host.port(), kSessionKey, 0, kLinkTimeoutMs));
    REQUIRE(waitUntil([&host] { return host.keepalives() > 0; }));

    host.loseNextDatagram();
    REQUIRE(waitUntil([&host] { return host.losing(); }));
    const auto sentAt = Clock::now();
    link.sendControllerState(ControllerState{});
    REQUIRE(waitUntil([&host] { return host.inputs() == 1; }));
    const auto arrivedAfter = Clock::now() - sentAt;

    CHECK(host.datagramsLost() == 1);
    CHECK(arrivedAfter < kResendBound);
}

TEST_CASE("A host that ends the link is reported as a closed link", "[moonlight][rxlock]") {
    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    std::atomic<int> calls{0};
    std::atomic<bool> terminated{true};
    MoonlightControlChannel link;
    link.setDisconnectHandler([&calls, &terminated](bool wasTerminated) {
        terminated.store(wasTerminated);
        calls.fetch_add(1);
    });
    REQUIRE(link.connect("127.0.0.1", host.port(), kSessionKey, 0, kLinkTimeoutMs));

    host.hangUp();

    REQUIRE(waitUntil([&calls] { return calls.load() > 0; }));
    CHECK(calls.load() == 1);
    CHECK_FALSE(terminated.load());
    CHECK_FALSE(link.isConnected());
}

TEST_CASE("A host that says TERMINATION before it ends the link is reported as terminated",
          "[moonlight][rxlock]") {
    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    std::atomic<int> calls{0};
    std::atomic<bool> terminated{false};
    MoonlightControlChannel link;
    link.setDisconnectHandler([&calls, &terminated](bool wasTerminated) {
        terminated.store(wasTerminated);
        calls.fetch_add(1);
    });
    REQUIRE(link.connect("127.0.0.1", host.port(), kSessionKey, 0, kLinkTimeoutMs));

    host.send(dish::moonlight::encodeTermination());
    host.hangUp();

    REQUIRE(waitUntil([&calls] { return calls.load() > 0; }));
    CHECK(calls.load() == 1);
    CHECK(terminated.load());
    CHECK_FALSE(link.isConnected());
}
