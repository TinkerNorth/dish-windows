// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The host's touchpad pick decides whether a pad's touches reach a Moonlight host, as it decides
// what a satellite's descriptor declares: Pad, or a host never picked for, sends them, and Off
// keeps them. The pick is read when the slot binds, which Apply does after writing it. The cases
// run the real manager, session and control link against MoonlightWolfControlHost, which records
// every CONTROLLER_TOUCH it reads.
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

#include <memory>
#include <optional>
#include <string>

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
// was NOT sent.
constexpr int kQuietMs = 300;

int touchesAfter(const MoonlightWolfControlHost& host, std::size_t seen, std::uint8_t touchEvent) {
    int count = 0;
    const auto packets = host.packets();
    for (std::size_t i = seen; i < packets.size(); ++i) {
        const bool matches = packets[i].inputType == dish::moonlight::kInputControllerTouch &&
                             packets[i].touchEvent == touchEvent;
        if (matches) { ++count; }
    }
    return count;
}

int downsAfter(const MoonlightWolfControlHost& host, std::size_t seen) {
    return touchesAfter(host, seen, dish::moonlight::kTouchEventDown);
}

// Every CONTROLLER_TOUCH, whatever it says: the link carries keepalives of its own besides.
int anyTouchesAfter(const MoonlightWolfControlHost& host, std::size_t seen) {
    int count = 0;
    const auto packets = host.packets();
    for (std::size_t i = seen; i < packets.size(); ++i) {
        if (packets[i].inputType == dish::moonlight::kInputControllerTouch) { ++count; }
    }
    return count;
}

// A pad with a touchpad on a manager whose host's touchpad pick is `pick`.
struct Fixture {
    MoonlightWolfControlHost host{kRikey};
    std::shared_ptr<QSettings> settings;
    std::unique_ptr<MoonlightManager> manager;
    std::optional<std::string> pick;

    Fixture() : settings(makeSharedSettings()) {
        ensureApp();
        manager = std::make_unique<MoonlightManager>(settings);
        manager->addManualHost(hostIp(), QStringLiteral("Study PC"));
        manager->setTouchpadPick([this](const QString&) { return pick; });
    }

    MoonlightSession* session() const {
        for (auto* candidate : manager->findChildren<MoonlightSession*>()) {
            if (candidate->host().ip == hostIp()) { return candidate; }
        }
        return nullptr;
    }

    // What the binding flow's Apply ends in, for a pad with a touchpad and a gyro.
    BindOutcome bind(int devicePick) const {
        return manager->bindSlot(slotId(), hostId(), devicePick, /*hasRumble=*/true,
                                 /*hasMotion=*/true, /*hasTouchpad=*/true, /*hasBattery=*/true,
                                 /*hasLightbar=*/true);
    }

    // The pad bound and its session streaming, with the host holding the pad: its arrival and the
    // neutral state behind it have been read.
    bool bindLive(int devicePick) {
        if (!host.listening() || bind(devicePick) != BindOutcome::Bound) { return false; }
        auto* live = session();
        if (live == nullptr) { return false; }
        if (!MoonlightSessionTestAccess::control(*live).connect("127.0.0.1", host.port(), kRikey, 0,
                                                                kLinkTimeoutMs)) {
            return false;
        }
        MoonlightSessionTestAccess::settle(*live, SessionPhase::ControlConnecting);
        MoonlightSessionTestAccess::feed(*live, SessionEvent::ControlConnected);
        return pumpUntil([this] { return host.padType(0).has_value(); });
    }

    // One full-state frame, the way the input thread hands it over: finger 7 down at the given
    // spot, or nothing on the pad.
    void fingerDown() const {
        manager->forwardTouch(slotId().toStdString(), true, 7, 100, 200, false, 0, 0, 0);
    }
    void nothingTouching() const {
        manager->forwardTouch(slotId().toStdString(), false, 0, 0, 0, false, 0, 0, 0);
    }
};

} // namespace

TEST_CASE("A host never picked for gets a PlayStation pad's touches", "[moonlight][touchpad]") {
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    const std::size_t before = fx.host.packets().size();

    fx.fingerDown();

    CHECK(pumpUntil([&fx, before] { return downsAfter(fx.host, before) == 1; }));
}

TEST_CASE("A host picked Off gets none of the pad's touches", "[moonlight][touchpad]") {
    Fixture fx;
    fx.pick = "off";
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    const std::size_t before = fx.host.packets().size();

    fx.fingerDown();

    pumpFor(kQuietMs);
    CHECK(anyTouchesAfter(fx.host, before) == 0);
}

TEST_CASE("A pad bound as a type with no touchpad sends no touches", "[moonlight][touchpad]") {
    // The Xbox pad a host builds has no touchpad to put them on.
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDeviceXbox));
    const std::size_t before = fx.host.packets().size();

    fx.fingerDown();

    pumpFor(kQuietMs);
    CHECK(anyTouchesAfter(fx.host, before) == 0);
}

TEST_CASE("With no touchpad pick to read, a PlayStation pad's touches go out",
          "[moonlight][touchpad]") {
    // A manager nobody handed the store reads every host as never picked for.
    Fixture fx;
    fx.manager->setTouchpadPick(MoonlightManager::TouchpadPick{});
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    const std::size_t before = fx.host.packets().size();

    fx.fingerDown();

    CHECK(pumpUntil([&fx, before] { return downsAfter(fx.host, before) == 1; }));
}

TEST_CASE("A slot bound again reads its host's touchpad pick again", "[moonlight][touchpad]") {
    // Apply writes the pick and then binds, and a slot already on the session keeps its number:
    // the bind is where the new pick has to be read.
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    fx.pick = "off";

    REQUIRE(fx.bind(dish::models::kMoonlightDevicePlayStation) == BindOutcome::Bound);
    const std::size_t before = fx.host.packets().size();
    fx.fingerDown();

    pumpFor(kQuietMs);
    CHECK(anyTouchesAfter(fx.host, before) == 0);
}

TEST_CASE("Turning the touchpad off lifts the finger the host holds, and on puts it down again",
          "[moonlight][touchpad]") {
    // The host keeps a contact until it is told the finger left, so turning the touches off
    // mid-press must not leave it pressed there.
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    fx.fingerDown();
    REQUIRE(pumpUntil([&fx] { return downsAfter(fx.host, 0) == 1; }));

    fx.pick = "off";
    REQUIRE(fx.bind(dish::models::kMoonlightDevicePlayStation) == BindOutcome::Bound);
    std::size_t before = fx.host.packets().size();
    fx.fingerDown();

    CHECK(pumpUntil([&fx, before] {
        return touchesAfter(fx.host, before, dish::moonlight::kTouchEventUp) == 1;
    }));
    CHECK(downsAfter(fx.host, before) == 0);

    fx.pick = "ds4";
    REQUIRE(fx.bind(dish::models::kMoonlightDevicePlayStation) == BindOutcome::Bound);
    before = fx.host.packets().size();
    fx.fingerDown();

    CHECK(pumpUntil([&fx, before] { return downsAfter(fx.host, before) == 1; }));
}

TEST_CASE("A finger held through a re-apply is lifted on the host when it lifts",
          "[moonlight][touchpad]") {
    // Applying a binding again keeps the pad under its number, and the host keeps the contact it
    // was told about, so the frame the host was last told about has to survive the bind.
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    fx.fingerDown();
    REQUIRE(pumpUntil([&fx] { return downsAfter(fx.host, 0) == 1; }));

    REQUIRE(fx.bind(dish::models::kMoonlightDevicePlayStation) == BindOutcome::Bound);
    const std::size_t before = fx.host.packets().size();
    fx.nothingTouching();

    CHECK(pumpUntil([&fx, before] {
        return touchesAfter(fx.host, before, dish::moonlight::kTouchEventUp) == 1;
    }));
}

TEST_CASE("A pad's touch starts from nothing on a stream that comes up again",
          "[moonlight][touchpad]") {
    // The host builds its pads afresh for a new stream, so a finger that stayed down across it is
    // a contact the new pad never saw go down.
    Fixture fx;
    REQUIRE(fx.bindLive(dish::models::kMoonlightDevicePlayStation));
    fx.fingerDown();
    REQUIRE(pumpUntil([&fx] { return downsAfter(fx.host, 0) == 1; }));
    auto* live = fx.session();
    MoonlightSessionTestAccess::settle(*live, SessionPhase::ControlConnecting);
    MoonlightSessionTestAccess::feed(*live, SessionEvent::ControlConnected);
    const std::size_t before = fx.host.packets().size();

    fx.fingerDown();

    CHECK(pumpUntil([&fx, before] { return downsAfter(fx.host, before) == 1; }));
}
