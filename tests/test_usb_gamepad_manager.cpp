// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbGamepadManagerTest (ADAPT, 7). Re-derivation of dish-android source/usb/
// UsbGamepadManagerTest.kt against the Windows raw-HID claim driver with a FAKE
// gateway (no real USB/HID IO). The android test mocks the Android USB layer to
// drive a real open/claim/attach outcome into a DirectClaimFailure; here the
// FakeGateway returns a programmed ClaimResult so the SAME classification +
// auto-Direct gating is exercised through the public path-choice entry points:
//
//   * open rejected            -> Busy, drops back to Standard, markDirectFailed.
//   * permission denied        -> PermissionDenied.
//   * claim interface rejected -> Busy (framework not stolen).
//   * read-loop bring-up fails  -> waits for the framework as InitFailed (stolen).
//   * successful claim         -> Direct, a synthetic is registered.
//   * a recorded failure suppresses auto-Direct on a verified model (no claim).
//   * a verified model with no recorded failure auto-claims Direct (claim runs).

#include "source/usb/UsbGamepadManager.h"

#include "source/store/UsbPathPreferenceStore.h"
#include "source/usb/UsbDeviceGateway.h"

#include "Input/GamepadInputProcessor.h"

#include "QSettingsFixture.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

using namespace dish::source::usb;
using dish::reducer::DirectClaimFailure;
using dish::reducer::PathChoice;
using dish::reducer::UsbPhase;
using dish::source::UsbPathPreferenceRepository;
using dish::source::UsbPathPreferenceStore;
using dish::test::makeSharedSettings;

namespace {

constexpr int kVid = 0x045E, kPid = 0x028E;

UsbDeviceInfo padDevice() {
    UsbDeviceInfo d;
    d.vendorId = kVid;
    d.productId = kPid;
    d.name = "Pad";
    d.endpointInMaxPacket = 64;
    d.endpointInInterval = 1;
    return d;
}

// A programmable gateway: enumerate() returns the seeded device(s), claim()
// returns the seeded outcome (and records that a claim was attempted), and the
// fast-lane verdict is settable per test.
class FakeGateway : public UsbDeviceGateway {
  public:
    std::vector<UsbDeviceInfo> devices{padDevice()};
    std::optional<ClaimResult> nextClaim; // nullopt -> a default Busy.
    bool fastLane = false;
    int claimCalls = 0;
    int nextSyntheticId = -1000;

    std::vector<UsbDeviceInfo> enumerate() override { return devices; }
    ClaimResult claim(const UsbDeviceInfo& /*d*/,
                      std::function<void(const UsbReport&)> /*onReport*/) override {
        ++claimCalls;
        if (nextClaim.has_value()) { return *nextClaim; }
        return ClaimResult::fail(DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    }
    void releaseClaim(int /*syntheticId*/) override {}
    bool isKnownFastLaneModel(int /*vendorId*/, int /*productId*/) const override {
        return fastLane;
    }
    std::int64_t completionCount(int /*syntheticId*/) const override { return 0; }
};

// Records the side-effect observer calls (markFailure, syntheticAdded, ...).
struct RecordingObserver : UsbDirectObserver {
    std::vector<DirectClaimFailure> failures;
    std::vector<int> syntheticsAdded;
    void markFailure(int /*v*/, int /*p*/, DirectClaimFailure reason) override {
        failures.push_back(reason);
    }
    void syntheticAdded(int syntheticId, const std::string& /*name*/, bool /*hasGyro*/,
                        int /*pollRateHz*/, int /*v*/, int /*p*/) override {
        syntheticsAdded.push_back(syntheticId);
    }
};

bool sawFailure(const RecordingObserver& obs, DirectClaimFailure reason) {
    for (auto f : obs.failures) {
        if (f == reason) { return true; }
    }
    return false;
}

} // namespace

TEST_CASE("open rejected reports Busy and drops back to Standard", "[usb-manager]") {
    FakeGateway gw;
    gw.nextClaim = ClaimResult::fail(DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    RecordingObserver obs;
    UsbGamepadManager m(&gw, nullptr, &prefs, &obs);

    m.tryDirectMode(kVid, kPid);

    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->phase == UsbPhase::Routed);
    CHECK(c->failure == DirectClaimFailure::Busy);
    CHECK(sawFailure(obs, DirectClaimFailure::Busy));
}

TEST_CASE("permission denied reports PermissionDenied", "[usb-manager]") {
    FakeGateway gw;
    gw.nextClaim =
        ClaimResult::fail(DirectClaimFailure::PermissionDenied, /*frameworkStolen=*/false);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    RecordingObserver obs;
    UsbGamepadManager m(&gw, nullptr, &prefs, &obs);

    m.tryDirectMode(kVid, kPid);

    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->phase == UsbPhase::Routed);
    CHECK(c->failure == DirectClaimFailure::PermissionDenied);
    CHECK(sawFailure(obs, DirectClaimFailure::PermissionDenied));
}

TEST_CASE("claim interface rejected reports Busy", "[usb-manager]") {
    FakeGateway gw;
    gw.nextClaim = ClaimResult::fail(DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, nullptr, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->failure == DirectClaimFailure::Busy);
    CHECK(c->phase == UsbPhase::Routed);
}

TEST_CASE("read-loop bring-up failure after claim waits for the framework as InitFailed",
          "[usb-manager]") {
    FakeGateway gw;
    // The interface was stolen (claimed) but the read loop never came up.
    gw.nextClaim = ClaimResult::fail(DirectClaimFailure::InitFailed, /*frameworkStolen=*/true);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, nullptr, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->phase == UsbPhase::AwaitingFramework);
    CHECK(c->failure == DirectClaimFailure::InitFailed);
}

TEST_CASE("successful claim reaches Direct and registers a synthetic", "[usb-manager]") {
    FakeGateway gw;
    gw.nextClaim = ClaimResult::success(-1000);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    RecordingObserver obs;
    UsbGamepadManager m(&gw, nullptr, &prefs, &obs);

    m.tryDirectMode(kVid, kPid);

    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->phase == UsbPhase::Direct);
    CHECK(c->syntheticId == -1000);
    CHECK_FALSE(c->failure.has_value());
    REQUIRE(obs.syntheticsAdded.size() == 1);
    CHECK(obs.syntheticsAdded.front() == -1000);
}

TEST_CASE("a recorded failure suppresses auto-Direct on a verified model", "[usb-manager]") {
    FakeGateway gw;
    gw.fastLane = true;
    // A first auto-claim that fails records the prior failure (Busy, not stolen).
    gw.nextClaim = ClaimResult::fail(DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, nullptr, &prefs, nullptr);

    m.reconcile(); // verified model -> auto-Direct -> claim runs and fails.
    REQUIRE(gw.claimCalls == 1);
    {
        const auto c = m.controllerFor(kVid, kPid);
        REQUIRE(c.has_value());
        CHECK(c->desired == PathChoice::Standard); // rolled back after the failure.
    }

    // A subsequent reconcile (e.g. re-foreground) must NOT auto-claim again: the
    // recorded failure gates auto-Direct, so resolvePath settles Standard.
    m.reconcile();
    CHECK(gw.claimCalls == 1); // no second claim attempt.
    const auto c = m.controllerFor(kVid, kPid);
    REQUIRE(c.has_value());
    CHECK(c->desired == PathChoice::Standard);
}

TEST_CASE("a verified model with no recorded failure auto-claims Direct", "[usb-manager]") {
    FakeGateway gw;
    gw.fastLane = true;
    gw.nextClaim = ClaimResult::fail(DirectClaimFailure::Busy, /*frameworkStolen=*/false);
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, nullptr, &prefs, nullptr);

    m.reconcile();

    // The auto path attempted the claim (the gateway's claim was reached) instead
    // of settling Standard without trying.
    CHECK(gw.claimCalls == 1);
}

namespace {

// A gateway that drives a single decoded report through the onReport callback on
// claim, so the manager's publish path (INPUT + MOTION + TOUCHPAD into the real
// GamepadInputProcessor) can be asserted with no real HID IO.
class ReportingGateway : public UsbDeviceGateway {
  public:
    UsbReport report; // the report claim() will emit.
    int syntheticId = -1000;

    std::vector<UsbDeviceInfo> enumerate() override { return {padDevice()}; }
    ClaimResult claim(const UsbDeviceInfo& /*d*/,
                      std::function<void(const UsbReport&)> onReport) override {
        if (onReport) { onReport(report); }
        return ClaimResult::success(syntheticId);
    }
    void releaseClaim(int /*syntheticId*/) override {}
    bool isKnownFastLaneModel(int /*v*/, int /*p*/) const override { return false; }
    std::int64_t completionCount(int /*syntheticId*/) const override { return 0; }
};

} // namespace

TEST_CASE("a claimed report publishes INPUT through the processor on the claim path",
          "[usb-manager]") {
    ReportingGateway gw;
    gw.report.wButtons = dish::input::GamepadInputProcessor::Buttons::kA;
    gw.report.lt = 200;
    gw.report.lx = 12345;

    dish::input::GamepadInputProcessor processor;
    std::string gotId;
    std::uint16_t gotButtons = 0;
    std::uint8_t gotLt = 0;
    std::int16_t gotLx = 0;
    processor.setReportSender([&](const std::string& id, std::uint16_t b, std::uint8_t lt,
                                  std::uint8_t /*rt*/, std::int16_t lx, std::int16_t /*ly*/,
                                  std::int16_t /*rx*/, std::int16_t /*ry*/) {
        gotId = id;
        gotButtons = b;
        gotLt = lt;
        gotLx = lx;
    });
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, &processor, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    // The synthetic slot id the read loop publishes under is the model vpKey string.
    const std::string expectId = std::to_string((kVid << 16) | (kPid & 0xFFFF));
    CHECK(gotId == expectId);
    CHECK(gotButtons == dish::input::GamepadInputProcessor::Buttons::kA);
    CHECK(gotLt == 200);
    CHECK(gotLx == 12345);
}

TEST_CASE("a claimed report with IMU publishes MOTION through the processor", "[usb-manager]") {
    ReportingGateway gw;
    gw.report.motionValid = true;
    gw.report.gyroX = 111;
    gw.report.accelZ = 222;

    dish::input::GamepadInputProcessor processor;
    bool gotMotion = false;
    std::int16_t gotGyroX = 0;
    std::int16_t gotAccelZ = 0;
    processor.setMotionSender([&](const std::string& /*id*/, std::int16_t gx, std::int16_t /*gy*/,
                                  std::int16_t /*gz*/, std::int16_t /*ax*/, std::int16_t /*ay*/,
                                  std::int16_t az, std::uint32_t /*dt*/) {
        gotMotion = true;
        gotGyroX = gx;
        gotAccelZ = az;
    });
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, &processor, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    CHECK(gotMotion);
    CHECK(gotGyroX == 111);
    CHECK(gotAccelZ == 222);
}

TEST_CASE("a claimed report without IMU does not publish MOTION", "[usb-manager]") {
    ReportingGateway gw;
    gw.report.motionValid = false;
    gw.report.gyroX = 999; // present but must be ignored (motionValid gates it).

    dish::input::GamepadInputProcessor processor;
    bool gotMotion = false;
    processor.setMotionSender([&](const std::string&, std::int16_t, std::int16_t, std::int16_t,
                                  std::int16_t, std::int16_t, std::int16_t,
                                  std::uint32_t) { gotMotion = true; });
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, &processor, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    CHECK_FALSE(gotMotion);
}

TEST_CASE("a claimed report with a touchpad publishes TOUCHPAD through the processor",
          "[usb-manager]") {
    ReportingGateway gw;
    gw.report.touchpadValid = true;
    gw.report.finger0Active = true;
    gw.report.finger0Id = 7;
    gw.report.finger0X = -100;
    gw.report.touchpadButton = true;

    dish::input::GamepadInputProcessor processor;
    bool gotTouch = false;
    dish::input::GamepadInputProcessor::TouchpadSample got{};
    processor.setTouchpadSender([&](const std::string& /*id*/,
                                    const dish::input::GamepadInputProcessor::TouchpadSample& s) {
        gotTouch = true;
        got = s;
    });
    UsbPathPreferenceRepository repo(makeSharedSettings());
    UsbPathPreferenceStore prefs(&repo);
    UsbGamepadManager m(&gw, &processor, &prefs, nullptr);

    m.tryDirectMode(kVid, kPid);

    REQUIRE(gotTouch);
    CHECK(got.finger0Active);
    CHECK(got.finger0Id == 7);
    CHECK(got.finger0X == -100);
    CHECK(got.buttonPressed);
}
