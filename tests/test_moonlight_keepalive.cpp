// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What keeps a live Moonlight session alive at the host, and what stops it.
//
// A host ends a session whose control stream it has heard nothing on for its
// ping timeout: Sunshine's is ten seconds, and ENet's own pings do not reset it,
// only a control message does. Whether that silence can happen is a question
// about THREADS rather than the wire, because the thread that owns a session
// here is the UI thread, and the UI thread can be busy. So the host in these
// cases listens on a thread of its own (MoonlightFakeControlHost), and the
// session's thread is kept busy on purpose.
//
// The control link runs on loopback; the host record itself is TEST-NET-1
// (RFC 5737), so the /cancel a quit sends goes nowhere.

#include "Network/MoonlightSession.h"
#include "core/moonlight/MoonlightIdentity.h"
#include "core/moonlight/MoonlightSessionMachine.h"

#include "MoonlightFakeControlHost.h"
#include "MoonlightSessionTestAccess.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QString>

#include <array>
#include <chrono>
#include <cstdint>
#include <thread>

using dish::models::MoonlightHost;
using dish::moonlight::SessionPhase;
using dish::net::MoonlightSession;
using dish::net::MoonlightSessionTestAccess;
using dish::test::MoonlightFakeControlHost;

namespace {

// MoonlightSession builds a QNetworkAccessManager, whose app-static factory
// asserts unless a QCoreApplication exists, and Catch2WithMain creates none.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

// Turn the event loop until `done` or the deadline, so whatever the session runs
// on its own thread keeps running while a case waits.
template <class Fn> bool pumpUntil(Fn done, int budgetMs = 5000) {
    QDeadlineTimer deadline(budgetMs);
    while (!deadline.hasExpired()) {
        if (done()) { return true; }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }
    return done();
}

void pumpFor(int ms) {
    QDeadlineTimer deadline(ms);
    while (!deadline.hasExpired()) { QCoreApplication::processEvents(QEventLoop::AllEvents, 20); }
}

// Nothing the session owns on this thread runs until this returns: its timers
// and its queued calls wait, the way they do behind a UI thread that is stuck.
void keepThisThreadBusy(std::chrono::milliseconds span) { std::this_thread::sleep_for(span); }

const std::array<std::uint8_t, 16> kSessionKey = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                                                  0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};

// Six keepalive periods of a client that sends one every half second. Three is
// the least that proves the keepalive is still being sent rather than that one
// was in flight when the thread stopped, with room for a loaded machine.
constexpr std::chrono::milliseconds kBusyFor{3000};
constexpr int kHeardWhileBusyAtLeast = 3;

// More than two keepalive periods: long enough that a keepalive still being sent
// would have arrived.
constexpr int kQuietAfterQuitMs = 1200;

// The record the session is built from. TEST-NET-1, so nothing but the control
// link, which the case dials on loopback, reaches a machine.
MoonlightHost unroutableHost() {
    MoonlightHost h;
    h.name = QStringLiteral("Study PC");
    h.ip = QStringLiteral("192.0.2.31");
    return h;
}

dish::moonlight::Identity clientIdentity() {
    const auto identity = dish::moonlight::generateIdentity();
    REQUIRE(identity.has_value());
    return *identity;
}

// A session live on a control link to `host`, as it stands once the stream is up.
struct LiveSession {
    MoonlightSession session;

    explicit LiveSession(const MoonlightFakeControlHost& host)
        : session(unroutableHost(), clientIdentity(), nullptr) {
        REQUIRE(MoonlightSessionTestAccess::connectControl(session, "127.0.0.1", host.port(),
                                                           kSessionKey));
        REQUIRE(session.phase() == SessionPhase::Streaming);
    }
};

} // namespace

TEST_CASE("A live session keeps the host hearing from it while its own thread is busy",
          "[moonlight][keepalive]") {
    ensureApp();
    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    LiveSession live(host);
    REQUIRE(pumpUntil([&host] { return host.keepalives() > 0; }));

    const int heardBefore = host.keepalives();
    keepThisThreadBusy(kBusyFor);
    const int heardWhileBusy = host.keepalives() - heardBefore;

    REQUIRE(heardWhileBusy >= kHeardWhileBusyAtLeast);
}

TEST_CASE("A session that quits stops keeping its link alive", "[moonlight][keepalive]") {
    ensureApp();
    MoonlightFakeControlHost host(kSessionKey);
    REQUIRE(host.listening());
    LiveSession live(host);
    REQUIRE(pumpUntil([&host] { return host.keepalives() > 0; }));

    live.session.quit();
    REQUIRE(live.session.phase() == SessionPhase::Closed);
    REQUIRE(pumpUntil([&host] { return host.clientLeft(); }));

    const int heardAtQuit = host.keepalives();
    pumpFor(kQuietAfterQuitMs);
    REQUIRE(host.keepalives() == heardAtQuit);
}
