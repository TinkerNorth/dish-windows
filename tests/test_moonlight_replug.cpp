// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A pad the host already holds is replugged, never announced twice (host fact H5).
//
// A Moonlight host keeps a controller number it holds and skips a second CONTROLLER_ARRIVAL for
// it, so a binding whose pad changes after it was announced (another emulated type, or motion the
// first arrival did not carry) reaches the host only as an unplug followed by a new arrival under
// the same number. The cases run the real manager, session and control link against
// MoonlightWolfControlHost, which keeps its joypad table by Wolf's rules, so what they assert is
// the pad the host ends up holding.
//
// NO LIVE HOST IS CONTACTED: the HTTP a bind starts goes to RFC 5737 TEST-NET-1 and the control
// link to loopback.

#include "Network/MoonlightControlChannel.h"
#include "Network/MoonlightManager.h"
#include "Network/MoonlightSession.h"
#include "repository/MoonlightHostRepository.h"

#include "MoonlightRequestLog.h"
#include "MoonlightSessionTestAccess.h"
#include "MoonlightWolfControlHost.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QString>

#include <memory>

using dish::moonlight::BindOutcome;
using dish::moonlight::SessionEvent;
using dish::moonlight::SessionPhase;
using dish::net::MoonlightControlChannel;
using dish::net::MoonlightManager;
using dish::net::MoonlightSession;
using dish::net::MoonlightSessionTestAccess;
using dish::test::makeSharedSettings;
using dish::test::MoonlightRequestLog;
using dish::test::MoonlightWolfControlHost;

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

// Every wait is bounded: a case that hangs waiting for a host is worse than one that fails.
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

// The rikey a /launch would have carried: the control stream is sealed with it.
const MoonlightWolfControlHost::Key kRikey = {0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
                                              0x98, 0xA9, 0xBA, 0xCB, 0xDC, 0xED, 0xFE, 0x0F};
const QString kIpA = QStringLiteral("192.0.2.11");
const QString kIdA = QStringLiteral("ml:ip:192.0.2.11");
const QString kCancel = QStringLiteral("/cancel");
const QString kLaunch = QStringLiteral("/launch");
constexpr int kLinkTimeoutMs = 2000;
// Long enough for a packet sent on loopback to have been read, for the cases that assert one
// was NOT sent.
constexpr int kQuietMs = 300;

using dish::moonlight::kInputControllerArrival;
using dish::moonlight::kInputControllerMulti;
using dish::moonlight::kPadTypePlayStation;
using dish::moonlight::kPadTypeXbox;

// The packets that name a pad, which leaves out the keepalive a live stream sends on its own.
int padPacketsAfter(const MoonlightWolfControlHost& host, std::size_t seen) {
    int count = 0;
    const auto packets = host.packets();
    for (std::size_t i = seen; i < packets.size(); ++i) {
        const bool namesAPad = packets[i].inputType == kInputControllerArrival ||
                               packets[i].inputType == kInputControllerMulti;
        if (namesAPad) { ++count; }
    }
    return count;
}

// A pad the host was never told about, on the manager a binding flow drives.
struct Fixture {
    MoonlightWolfControlHost host{kRikey};
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<MoonlightManager> manager;

    Fixture() : settings(makeSharedSettings()) {
        ensureApp();
        manager = std::make_unique<MoonlightManager>(settings);
        manager->addManualHost(kIpA, QStringLiteral("Study PC"));
    }

    MoonlightSession* session() const {
        for (auto* candidate : manager->findChildren<MoonlightSession*>()) {
            if (candidate->host().ip == kIpA) { return candidate; }
        }
        return nullptr;
    }

    // The bind the binding flow makes, for a pad with rumble, a touchpad and a lightbar.
    BindOutcome bind(const QString& slotId, int devicePick, bool motion, bool battery) const {
        return manager->bindSlot(slotId, kIdA, devicePick, /*hasRumble=*/true, motion,
                                 /*hasTouchpad=*/true, battery, /*hasLightbar=*/true);
    }

    bool connectLink(MoonlightSession& live) {
        return MoonlightSessionTestAccess::control(live).connect("127.0.0.1", host.port(), kRikey,
                                                                 0, kLinkTimeoutMs);
    }

    // The stream comes up the way it does live: the control link connects and the reducer's
    // effects announce every pad the session remembers.
    static void streamUp(MoonlightSession& live) {
        MoonlightSessionTestAccess::settle(live, SessionPhase::ControlConnecting);
        MoonlightSessionTestAccess::feed(live, SessionEvent::ControlConnected);
    }

    // A session on a live stream whose host holds the pads bound so far.
    bool bringLive(MoonlightSession& live) {
        if (!connectLink(live)) { return false; }
        streamUp(live);
        return true;
    }

    // Every pad the stream came up with has reached the host: its arrival, and the neutral state
    // sent right behind it.
    bool announced(int pads) const {
        return pumpUntil([this, pads] { return padPacketsAfter(host, 0) == 2 * pads; });
    }
};

} // namespace

TEST_CASE("A second arrival for a number the host holds is skipped", "[moonlight][replug][h5]") {
    // What makes a replug necessary at all, against the real link: announcing a number again is
    // not a way to change the pad under it.
    ensureApp();
    MoonlightWolfControlHost host(kRikey);
    REQUIRE(host.listening());
    MoonlightControlChannel link;
    REQUIRE(link.connect("127.0.0.1", host.port(), kRikey, 0, kLinkTimeoutMs));

    link.sendControllerArrival(0, kPadTypeXbox, 0x03, 0xFFFF);
    link.sendControllerArrival(0, kPadTypePlayStation, 0x3B, 0xFFFF);

    REQUIRE(pumpUntil([&host] { return padPacketsAfter(host, 0) == 2; }));
    CHECK(host.padType(0) == kPadTypeXbox);
}

TEST_CASE("A replug unplugs the number and plugs the new pad in, back to back",
          "[moonlight][replug][h5]") {
    ensureApp();
    MoonlightWolfControlHost host(kRikey);
    REQUIRE(host.listening());
    MoonlightControlChannel link;
    REQUIRE(link.connect("127.0.0.1", host.port(), kRikey, 0, kLinkTimeoutMs));
    link.sendControllerArrival(0, kPadTypeXbox, 0x03, 0xFFFF);
    link.sendControllerArrival(1, kPadTypeXbox, 0x03, 0xFFFF);
    REQUIRE(pumpUntil([&host] { return padPacketsAfter(host, 0) == 2; }));

    link.sendControllerReplug(0, /*otherPadsMask=*/0b10, kPadTypePlayStation, 0x3B, 0xFFFF);

    REQUIRE(pumpUntil([&host] { return host.padType(0) == kPadTypePlayStation; }));
    // The pad beside it was never named, so it is exactly where it was.
    CHECK(host.padType(1) == kPadTypeXbox);
    const auto packets = host.packets();
    REQUIRE(packets.size() == 4);
    const auto& unplug = packets[2];
    const auto& arrival = packets[3];
    CHECK(unplug.inputType == kInputControllerMulti);
    CHECK(unplug.number == 0);
    CHECK(unplug.mask == 0b10);
    CHECK(arrival.inputType == kInputControllerArrival);
    CHECK(arrival.number == 0);
    // Consecutive sequence numbers: nothing was sealed between the two, so no input frame could
    // have plugged a default pad into the gap.
    CHECK(arrival.seq == unplug.seq + 1);
}

TEST_CASE("A live pad that changes type is replugged under its number and nothing closes",
          "[moonlight][replug][h5]") {
    // The binding flow's Apply on a pad that is already streaming. The pad was the only one on
    // the host, so an unbind-then-bind would have been the LAST pad off a live session: a /cancel
    // that closes the game the user is playing, and a fresh /launch behind it.
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, true, true) ==
            BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(fx.bringLive(*session));
    REQUIRE(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypeXbox; }));
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, true,
                    true) == BindOutcome::Bound);

    CHECK(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypePlayStation; }));
    CHECK(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    CHECK(session->phase() == SessionPhase::Streaming);
    CHECK(log.count(kCancel) == 0);
    CHECK(log.count(kLaunch) == 0);
}

TEST_CASE("A pad that changes type before the stream is up arrives as the new pad",
          "[moonlight][replug][h5]") {
    // Nothing is on the wire yet, so there is nothing to unplug: the remembered arrival changes
    // and the stream announces it when it comes up. And the launch already in flight is the one
    // the session rides: a second would be answered "an app is already running".
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(session->phase() == SessionPhase::Launching);
    REQUIRE(fx.connectLink(*session));
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, false,
                    false) == BindOutcome::Bound);

    CHECK(log.count(kLaunch) == 0);
    CHECK(log.count(kCancel) == 0);
    CHECK(session->phase() == SessionPhase::Launching);
    pumpFor(kQuietMs);
    CHECK(padPacketsAfter(fx.host, 0) == 0);

    Fixture::streamUp(*session);
    CHECK(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypePlayStation; }));
}

TEST_CASE("A re-bind that changes nothing the host reads leaves the pad as it is",
          "[moonlight][replug][h5]") {
    // A replug unplugs the pad in the game, so a change the host never reads at arrival is not
    // worth one: here the battery bit, which only decides whether charge reports are sent.
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, true,
                    false) == BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(fx.bringLive(*session));
    REQUIRE(fx.announced(1));
    REQUIRE(fx.host.padType(0) == kPadTypePlayStation);
    const auto announced = session->announcedPad(0);
    REQUIRE(announced.has_value());
    const std::size_t before = fx.host.packets().size();
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, true,
                    true) == BindOutcome::Bound);

    pumpFor(kQuietMs);
    CHECK(padPacketsAfter(fx.host, before) == 0);
    CHECK(session->announcedPad(0)->capabilities == announced->capabilities);
    CHECK(session->phase() == SessionPhase::Streaming);
    CHECK(log.count(kCancel) == 0);
    CHECK(log.count(kLaunch) == 0);
}

TEST_CASE("Replugging one pad leaves the pads beside it where they are",
          "[moonlight][replug][h5]") {
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);
    REQUIRE(fx.bind(QStringLiteral("sdl:2"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(fx.bringLive(*session));
    REQUIRE(fx.announced(2));
    const std::size_t before = fx.host.packets().size();
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:2"), dish::models::kMoonlightDevicePlayStation, false,
                    false) == BindOutcome::Bound);

    REQUIRE(pumpUntil([&fx] { return fx.host.padType(1) == kPadTypePlayStation; }));
    CHECK(fx.host.padType(0) == kPadTypeXbox);
    CHECK(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
    CHECK(fx.manager->slotForController(kIdA, 1) == QStringLiteral("sdl:2"));
    const auto packets = fx.host.packets();
    for (std::size_t i = before; i < packets.size(); ++i) {
        const bool unplugsPadZero = packets[i].inputType == kInputControllerMulti &&
                                    packets[i].number == 0 && (packets[i].mask & 0b01) == 0;
        CHECK_FALSE(unplugsPadZero);
    }
    CHECK(log.paths().isEmpty());
}

TEST_CASE("A replugged pad streams motion only once the pad the host built asks for it",
          "[moonlight][replug][h5]") {
    // The host asked the OLD pad for motion. The pad it builds in its place starts with no
    // subscription, and Wolf asks an Xbox pad for none at all.
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, true,
                    false) == BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(fx.bringLive(*session));
    REQUIRE(
        pumpUntil([session] { return session->motionWanted(0, dish::moonlight::kMotionGyro); }));

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, true, false) ==
            BindOutcome::Bound);
    REQUIRE(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypeXbox; }));

    pumpFor(kQuietMs);
    CHECK_FALSE(session->motionWanted(0, dish::moonlight::kMotionGyro));
    CHECK_FALSE(session->motionWanted(0, dish::moonlight::kMotionAccel));
}

TEST_CASE("A replugged pad's touch starts from nothing", "[moonlight][replug][h5]") {
    // A finger that stayed on the pad through the replug is a contact the new pad never saw go
    // down, so the next frame has to put it down again rather than move it.
    Fixture fx;
    REQUIRE(fx.host.listening());
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    REQUIRE(fx.bringLive(*session));
    REQUIRE(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypeXbox; }));
    const auto touch = [&fx] {
        fx.manager->forwardTouch("sdl:1", true, 7, 100, 200, false, 0, 0, 0);
    };
    touch();
    const auto downs = [&fx] {
        int count = 0;
        for (const auto& packet : fx.host.packets()) {
            const bool down = packet.inputType == dish::moonlight::kInputControllerTouch &&
                              packet.touchEvent == dish::moonlight::kTouchEventDown;
            if (down) { ++count; }
        }
        return count;
    };
    REQUIRE(pumpUntil([&downs] { return downs() == 1; }));

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDevicePlayStation, false,
                    false) == BindOutcome::Bound);
    REQUIRE(pumpUntil([&fx] { return fx.host.padType(0) == kPadTypePlayStation; }));
    touch();

    CHECK(pumpUntil([&downs] { return downs() == 2; }));
}

TEST_CASE("A slot bound again to a session that went down starts it again under its number",
          "[moonlight][replug]") {
    // Keeping the number must not cost the restart an unbind-then-bind used to give a stopped
    // session: the binding is still an intent to drive the host.
    Fixture fx;
    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);
    auto* session = fx.session();
    REQUIRE(session != nullptr);
    MoonlightSessionTestAccess::settle(*session, SessionPhase::Failed,
                                       dish::moonlight::SessionFailure::LinkDropped);
    MoonlightRequestLog log(*fx.manager);

    REQUIRE(fx.bind(QStringLiteral("sdl:1"), dish::models::kMoonlightDeviceXbox, false, false) ==
            BindOutcome::Bound);

    CHECK(log.count(kLaunch) == 1);
    CHECK(log.count(kCancel) == 0);
    CHECK(session->phase() == SessionPhase::Launching);
    CHECK(fx.manager->slotForController(kIdA, 0) == QStringLiteral("sdl:1"));
}
