// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A pad's Motion switch decides whether its motion reaches a Moonlight host, as dish-android's
// does: switched off, no sample goes out even while the host asks for them. The switch acts on the
// samples and never on the pad the host holds, so the arrival still declares the pad's sensors and
// turning motion back on costs no replug. The cases run the real manager, session and control link
// against MoonlightWolfControlHost, which asks a PlayStation pad for its motion the way Wolf does.
//
// NO LIVE HOST IS CONTACTED: the HTTP a bind starts goes to RFC 5737 TEST-NET-1 and the control
// link to loopback.

#include "Network/MoonlightManager.h"
#include "Network/MoonlightSession.h"

#include "MoonlightSessionTestAccess.h"
#include "MoonlightWolfControlHost.h"
#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QString>

#include <algorithm>
#include <memory>

using dish::moonlight::BindOutcome;
using dish::moonlight::SessionEvent;
using dish::moonlight::SessionPhase;
using dish::net::MoonlightManager;
using dish::net::MoonlightSession;
using dish::net::MoonlightSessionTestAccess;
using dish::test::makeSharedSettings;
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
QString hostIp() { return QStringLiteral("192.0.2.11"); }
QString hostId() { return QStringLiteral("ml:ip:192.0.2.11"); }
QString slotId() { return QStringLiteral("sdl:1"); }
constexpr int kLinkTimeoutMs = 2000;
// Long enough for a packet sent on loopback to have been read, for the cases that assert one
// was NOT sent, and longer than the gap the host's 100 Hz request leaves between two samples.
constexpr int kQuietMs = 300;

using dish::moonlight::kInputControllerArrival;
using dish::moonlight::kInputControllerMotion;
using dish::moonlight::kInputControllerMulti;

int packetsOfTypeAfter(const MoonlightWolfControlHost& host, std::size_t seen,
                       std::uint32_t inputType) {
    int count = 0;
    const auto packets = host.packets();
    for (std::size_t i = seen; i < packets.size(); ++i) {
        if (packets[i].inputType == inputType) { ++count; }
    }
    return count;
}

int motionPacketsAfter(const MoonlightWolfControlHost& host, std::size_t seen) {
    return packetsOfTypeAfter(host, seen, kInputControllerMotion);
}

// An arrival or a CONTROLLER_MULTI: what a replug is made of.
int padPacketsAfter(const MoonlightWolfControlHost& host, std::size_t seen) {
    return packetsOfTypeAfter(host, seen, kInputControllerArrival) +
           packetsOfTypeAfter(host, seen, kInputControllerMulti);
}

// A pad with a gyro, bound as a PlayStation pad, on a manager whose Motion switch answers
// `motionOn` for every slot.
struct Fixture {
    MoonlightWolfControlHost host{kRikey};
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<MoonlightManager> manager;
    bool motionOn = true;

    Fixture() : settings(makeSharedSettings()) {
        ensureApp();
        manager = std::make_unique<MoonlightManager>(settings);
        manager->addManualHost(hostIp(), QStringLiteral("Study PC"));
        manager->setMotionSwitch([this](const QString&) { return motionOn; });
    }

    MoonlightSession* session() const {
        for (auto* candidate : manager->findChildren<MoonlightSession*>()) {
            if (candidate->host().ip == hostIp()) { return candidate; }
        }
        return nullptr;
    }

    // What the binding flow's Apply ends in, for a pad with every sensor and actuator.
    BindOutcome bind() const {
        return manager->bindSlot(slotId(), hostId(), dish::models::kMoonlightDevicePlayStation,
                                 /*hasRumble=*/true, /*hasMotion=*/true, /*hasTouchpad=*/true,
                                 /*hasBattery=*/true, /*hasLightbar=*/true);
    }

    // The pad bound and its session streaming, with the host holding the pad and asking it for
    // both sensors. Null when any of that did not happen.
    MoonlightSession* bindLive() {
        if (!host.listening() || bind() != BindOutcome::Bound) { return nullptr; }
        auto* live = session();
        if (live == nullptr) { return nullptr; }
        if (!MoonlightSessionTestAccess::control(*live).connect("127.0.0.1", host.port(), kRikey, 0,
                                                                kLinkTimeoutMs)) {
            return nullptr;
        }
        MoonlightSessionTestAccess::settle(*live, SessionPhase::ControlConnecting);
        MoonlightSessionTestAccess::feed(*live, SessionEvent::ControlConnected);
        const bool asked = pumpUntil([live] {
            return live->motionWanted(0, dish::moonlight::kMotionGyro) &&
                   live->motionWanted(0, dish::moonlight::kMotionAccel);
        });
        return asked ? live : nullptr;
    }

    // One reading of both sensors, the way the input thread hands one over.
    void sendMotionSample() const {
        manager->forwardMotion(slotId().toStdString(), 1, 2, 3, 4, 5, 6);
    }
};

} // namespace

TEST_CASE("A pad whose Motion switch is on sends its motion once the host asks for it",
          "[moonlight][motion]") {
    Fixture fx;
    REQUIRE(fx.bindLive() != nullptr);
    const std::size_t before = fx.host.packets().size();

    fx.sendMotionSample();

    CHECK(pumpUntil([&fx, before] { return motionPacketsAfter(fx.host, before) == 2; }));
}

TEST_CASE("With no Motion switch set, a pad's motion goes out", "[moonlight][motion]") {
    // A manager nobody handed a switch has no answer of the user's to honour.
    Fixture fx;
    fx.manager->setMotionSwitch(MoonlightManager::MotionSwitch{});
    REQUIRE(fx.bindLive() != nullptr);
    const std::size_t before = fx.host.packets().size();

    fx.sendMotionSample();

    CHECK(pumpUntil([&fx, before] { return motionPacketsAfter(fx.host, before) == 2; }));
}

TEST_CASE("A pad whose Motion switch is off sends no motion while the host asks for it",
          "[moonlight][motion]") {
    Fixture fx;
    fx.motionOn = false;
    REQUIRE(fx.bindLive() != nullptr);
    const std::size_t before = fx.host.packets().size();

    fx.sendMotionSample();

    pumpFor(kQuietMs);
    CHECK(motionPacketsAfter(fx.host, before) == 0);
}

TEST_CASE("A pad whose Motion switch is off still arrives with its sensors",
          "[moonlight][motion]") {
    // The switch acts on the samples, as dish-android's does. An arrival without them would make
    // Wolf build another pad for the same number whenever the switch moved, and a replug unplugs
    // the pad in the game.
    Fixture fx;
    fx.motionOn = false;
    REQUIRE(fx.bindLive() != nullptr);

    const auto packets = fx.host.packets();
    const auto arrival = std::find_if(packets.cbegin(), packets.cend(), [](const auto& packet) {
        return packet.inputType == kInputControllerArrival;
    });
    REQUIRE(arrival != packets.cend());
    CHECK((arrival->capabilities & dish::moonlight::kCapsReadAtArrival) ==
          dish::moonlight::kCapsReadAtArrival);
    CHECK(fx.host.padType(0) == dish::moonlight::kPadTypePlayStation);
}

TEST_CASE("Turning motion off stops a stream that is already going", "[moonlight][motion]") {
    Fixture fx;
    REQUIRE(fx.bindLive() != nullptr);
    fx.sendMotionSample();
    REQUIRE(pumpUntil([&fx] { return motionPacketsAfter(fx.host, 0) == 2; }));

    fx.motionOn = false;
    fx.manager->refreshMotionSwitches();
    pumpFor(kQuietMs);
    const std::size_t before = fx.host.packets().size();
    fx.sendMotionSample();

    pumpFor(kQuietMs);
    CHECK(motionPacketsAfter(fx.host, before) == 0);
}

TEST_CASE("Turning motion back on lets the next sample through and replugs nothing",
          "[moonlight][motion]") {
    Fixture fx;
    fx.motionOn = false;
    REQUIRE(fx.bindLive() != nullptr);
    const std::size_t before = fx.host.packets().size();

    fx.motionOn = true;
    fx.manager->refreshMotionSwitches();
    fx.sendMotionSample();

    CHECK(pumpUntil([&fx, before] { return motionPacketsAfter(fx.host, before) == 2; }));
    CHECK(padPacketsAfter(fx.host, before) == 0);
}

TEST_CASE("A slot bound again reads its Motion switch again", "[moonlight][motion]") {
    // Apply writes the switch and then binds, and a slot already on the session keeps its number:
    // the bind is where the new answer has to be read.
    Fixture fx;
    REQUIRE(fx.bindLive() != nullptr);
    fx.motionOn = false;

    REQUIRE(fx.bind() == BindOutcome::Bound);
    const std::size_t before = fx.host.packets().size();
    fx.sendMotionSample();

    pumpFor(kQuietMs);
    CHECK(motionPacketsAfter(fx.host, before) == 0);
    CHECK(padPacketsAfter(fx.host, before) == 0);
}
