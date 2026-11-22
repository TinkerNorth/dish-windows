// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The bench's steps as this client's two paths write them (docs/diagnostics-bundle.md, "The
// bench"): the Direct claim writes the family's OUT report, and the SDL path hands a DualSense's
// SDL_GameControllerSendEffect the same report after its id. Both write the player-LED mask byte
// themselves, so the cycle is the contract's five masks on both; neither lights the LEDs by a
// player number (SDL_GameControllerSetPlayerIndex is not used, tests/PARITY.md section 5b).

#include "core/input/UsbOutputReports.h"
#include "core/model/Protocol.h"
#include "core/reducer/BenchSteps.h"
#include "core/reducer/BenchSurfaces.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

using dish::input::usbout::buildLightbarReport;
using dish::input::usbout::buildMicMuteLedReport;
using dish::input::usbout::buildPlayerLedsReport;
using dish::input::usbout::buildTriggerEffectsReport;
using dish::input::usbout::FeedbackState;
using dish::input::usbout::kDs5EffectBodyBytes;
using dish::input::usbout::kDs5EffectBodyOffset;
using dish::input::usbout::kMaxOutputReportBytes;
using dish::input::usbparse::HidParser;
using dish::reducer::BenchSurface;
using dish::reducer::BenchTest;
using dish::reducer::SurfaceWrite;

namespace {

using Report = std::array<std::uint8_t, kMaxOutputReportBytes>;

// DualSense report 0x02, id at byte 0: the light bar at 45..47, the player LEDs at 44, the mic
// lamp at 9, the right trigger's block at 11 and the left's at 22.
constexpr std::size_t kDs5PlayerLedsByte = 44;
constexpr std::size_t kDs5RedByte = 45;
constexpr std::size_t kDs5MicLampByte = 9;
constexpr std::size_t kDs5RightTriggerByte = 11;
constexpr std::size_t kDs5LeftTriggerByte = 22;
// The same fields in the body SDL_GameControllerSendEffect takes, one byte earlier.
constexpr std::size_t kBodyPlayerLedsByte = kDs5PlayerLedsByte - kDs5EffectBodyOffset;
constexpr std::size_t kBodyMicLampByte = kDs5MicLampByte - kDs5EffectBodyOffset;
constexpr std::size_t kBodyRightTriggerByte = kDs5RightTriggerByte - kDs5EffectBodyOffset;
constexpr std::size_t kBodyLeftTriggerByte = kDs5LeftTriggerByte - kDs5EffectBodyOffset;
// DualShock 4 report 0x05: the light bar at 6..8. Switch Pro report 0x01: subcommand 0x30's
// argument, the player lights, at 11.
constexpr std::size_t kDs4RedByte = 6;
constexpr std::size_t kSwitchPlayerLightsByte = 11;

// What one step of a test puts in each path's buffer, built as the claim's output worker and the
// SDL thread build it: from a fresh FeedbackState, so the DualSense's one-time light-bar handoff
// rides the first colour as it does on a real claim.
struct Built {
    Report report{};
    std::size_t bytes = 0;

    const std::uint8_t* body() const { return report.data() + kDs5EffectBodyOffset; }
};

Built builtFor(HidParser parser, FeedbackState& state, const SurfaceWrite& write) {
    Built built;
    const auto& v = write.values;
    switch (write.surface) {
    case BenchSurface::Lightbar:
        built.bytes = buildLightbarReport(parser, state, v.lightbar.r, v.lightbar.g, v.lightbar.b,
                                          built.report.data(), built.report.size());
        break;
    case BenchSurface::PlayerLeds:
        built.bytes = buildPlayerLedsReport(parser, state, v.playerLeds, /*seq=*/0,
                                            built.report.data(), built.report.size());
        break;
    case BenchSurface::TriggerEffects:
        built.bytes = buildTriggerEffectsReport(parser, state, v.triggerEffects.left.data(),
                                                v.triggerEffects.right.data(), built.report.data(),
                                                built.report.size());
        break;
    case BenchSurface::MicLamp:
        built.bytes = buildMicMuteLedReport(parser, state, v.micLamp, built.report.data(),
                                            built.report.size());
        break;
    }
    return built;
}

std::vector<SurfaceWrite> stepsOf(BenchTest test) {
    const auto plan = dish::reducer::surfacePlanFor(test);
    std::vector<SurfaceWrite> steps;
    if (!plan.has_value()) { return steps; }
    for (std::size_t step = 0; step < plan->steps; ++step) {
        steps.push_back(dish::reducer::surfaceStepFor(test, step));
    }
    return steps;
}

constexpr bool coversTheBody(std::size_t bytes) {
    return bytes >= kDs5EffectBodyOffset + kDs5EffectBodyBytes;
}

} // namespace

TEST_CASE("the player LED cycle reaches both of a DualSense's paths as the contract's five masks",
          "[diagnostics][bench][usb][output]") {
    FeedbackState state;
    std::vector<int> onTheClaim;
    std::vector<int> inSdlsBody;
    for (const SurfaceWrite& step : stepsOf(BenchTest::PlayerLedsCycle)) {
        const Built built = builtFor(HidParser::DualSense, state, step);
        REQUIRE(coversTheBody(built.bytes));
        onTheClaim.push_back(built.report[kDs5PlayerLedsByte]);
        inSdlsBody.push_back(built.body()[kBodyPlayerLedsByte]);
    }
    const std::vector<int> masks{0x01, 0x02, 0x04, 0x08, 0x10};
    CHECK(onTheClaim == masks);
    CHECK(inSdlsBody == masks);
}

// The Switch Pro has four player lights, and the Direct claim writes the mask masked to them, so
// the cycle's fifth mask lights none on it.
TEST_CASE("a Switch Pro on the Direct claim lights the first four masks and none for the fifth",
          "[diagnostics][bench][usb][output]") {
    FeedbackState state;
    std::vector<int> lights;
    for (const SurfaceWrite& step : stepsOf(BenchTest::PlayerLedsCycle)) {
        const Built built = builtFor(HidParser::SwitchProUsb, state, step);
        REQUIRE(built.bytes > kSwitchPlayerLightsByte);
        lights.push_back(built.report[kSwitchPlayerLightsByte]);
    }
    CHECK(lights == std::vector<int>{0x01, 0x02, 0x04, 0x08, 0x00});
}

TEST_CASE("the light bar cycle reaches a claimed DualSense and DualShock 4 as red, green, blue",
          "[diagnostics][bench][usb][output]") {
    FeedbackState dualSense;
    FeedbackState dualShock;
    std::vector<std::array<int, 3>> ds5;
    std::vector<std::array<int, 3>> ds4;
    for (const SurfaceWrite& step : stepsOf(BenchTest::LightbarCycle)) {
        const Built five = builtFor(HidParser::DualSense, dualSense, step);
        const Built four = builtFor(HidParser::DualShock4, dualShock, step);
        REQUIRE(five.bytes > kDs5RedByte + 2);
        REQUIRE(four.bytes > kDs4RedByte + 2);
        ds5.push_back(
            {five.report[kDs5RedByte], five.report[kDs5RedByte + 1], five.report[kDs5RedByte + 2]});
        ds4.push_back(
            {four.report[kDs4RedByte], four.report[kDs4RedByte + 1], four.report[kDs4RedByte + 2]});
    }
    const std::vector<std::array<int, 3>> cycle{{0xFF, 0, 0}, {0, 0xFF, 0}, {0, 0, 0xFF}};
    CHECK(ds5 == cycle);
    CHECK(ds4 == cycle);
}

TEST_CASE("the trigger pulse reaches both of a DualSense's paths as rigid resistance on both",
          "[diagnostics][bench][usb][output]") {
    FeedbackState state;
    const std::vector<SurfaceWrite> steps = stepsOf(BenchTest::TriggerPulse);
    REQUIRE(steps.size() == 1U);
    const Built built = builtFor(HidParser::DualSense, state, steps.front());
    REQUIRE(coversTheBody(built.bytes));
    for (const std::size_t block : {kDs5RightTriggerByte, kDs5LeftTriggerByte}) {
        CHECK(built.report[block] == 0x01);
        CHECK(built.report[block + 1] == 0x00);
        CHECK(built.report[block + 2] == 0xFF);
    }
    for (const std::size_t block : {kBodyRightTriggerByte, kBodyLeftTriggerByte}) {
        CHECK(built.body()[block] == 0x01);
        CHECK(built.body()[block + 2] == 0xFF);
    }
}

TEST_CASE("the lamp tests reach both of a DualSense's paths as on and as pulsing",
          "[diagnostics][bench][usb][output]") {
    FeedbackState state;
    const Built on = builtFor(HidParser::DualSense, state, stepsOf(BenchTest::LampOn).front());
    const Built pulse =
        builtFor(HidParser::DualSense, state, stepsOf(BenchTest::LampPulse).front());
    REQUIRE(coversTheBody(on.bytes));
    REQUIRE(coversTheBody(pulse.bytes));
    CHECK(on.report[kDs5MicLampByte] == dish::proto::kMicLedStateOn);
    CHECK(on.body()[kBodyMicLampByte] == dish::proto::kMicLedStateOn);
    CHECK(pulse.report[kDs5MicLampByte] == dish::proto::kMicLedStatePulse);
    CHECK(pulse.body()[kBodyMicLampByte] == dish::proto::kMicLedStatePulse);
}

// Rule 2 where the game never set a surface: Windows cannot read back what the pad showed (the HID
// class API reads input and feature reports, never an output report, and SDL2 has no getter for
// any of these), so the end writes the idle state, and each idle value is a write the pad takes,
// not one it ignores.
TEST_CASE("a surface the game never set goes back dark and off on a claimed DualSense",
          "[diagnostics][bench][usb][output]") {
    const dish::reducer::RememberedSurfaces nothingSet{};
    using dish::reducer::restoreWriteFor;
    const SurfaceWrite darkBar = restoreWriteFor(BenchSurface::Lightbar, nothingSet);
    const SurfaceWrite noLeds = restoreWriteFor(BenchSurface::PlayerLeds, nothingSet);
    const SurfaceWrite looseTriggers = restoreWriteFor(BenchSurface::TriggerEffects, nothingSet);
    const SurfaceWrite lampOff = restoreWriteFor(BenchSurface::MicLamp, nothingSet);
    FeedbackState state;
    const Built bar = builtFor(HidParser::DualSense, state, darkBar);
    const Built leds = builtFor(HidParser::DualSense, state, noLeds);
    const Built triggers = builtFor(HidParser::DualSense, state, looseTriggers);
    const Built lamp = builtFor(HidParser::DualSense, state, lampOff);
    REQUIRE(coversTheBody(bar.bytes));
    REQUIRE(coversTheBody(leds.bytes));
    REQUIRE(coversTheBody(triggers.bytes));
    REQUIRE(coversTheBody(lamp.bytes));
    CHECK((bar.report[2] & 0x04) != 0); // the colour is flagged, so the bar goes dark
    CHECK(bar.report[kDs5RedByte] == 0);
    CHECK(bar.report[kDs5RedByte + 1] == 0);
    CHECK(bar.report[kDs5RedByte + 2] == 0);
    CHECK((leds.report[2] & 0x10) != 0); // the LEDs are flagged, so they go out
    CHECK(leds.report[kDs5PlayerLedsByte] == 0);
    CHECK(triggers.report[1] == 0x0C); // both blocks flagged, effect mode 0: no resistance
    CHECK(triggers.report[kDs5RightTriggerByte] == 0);
    CHECK(triggers.report[kDs5LeftTriggerByte] == 0);
    CHECK((lamp.report[2] & 0x01) != 0); // the lamp is flagged, so it goes off
    CHECK(lamp.report[kDs5MicLampByte] == dish::proto::kMicLedStateOff);
}
