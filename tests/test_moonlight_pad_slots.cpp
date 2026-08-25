// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Controller-number allocation, the active mask (whose cleared bit IS the
// unplug signal), the arrival capability fold, and the device-pick mapping.

#include "Input/GamepadInputProcessor.h"
#include "core/moonlight/MoonlightPadSlots.h"

#include <catch2/catch_test_macros.hpp>

using namespace dish::moonlight;

TEST_CASE("PadSlots assigns the lowest free controller number", "[moonlight][padslots]") {
    PadSlots pads;
    REQUIRE(pads.assign("a").value() == 0);
    REQUIRE(pads.assign("b").value() == 1);
    REQUIRE(pads.assign("c").value() == 2);
    REQUIRE(pads.size() == 3);

    // Re-assigning a known slot is refused; the caller reads numberFor instead.
    REQUIRE_FALSE(pads.assign("a").has_value());
    REQUIRE(pads.numberFor("a").value() == 0);
    REQUIRE_FALSE(pads.numberFor("nope").has_value());

    // Releasing 1 frees it for the next assignment, so numbers are reused
    // rather than climbing past the host's ceiling.
    REQUIRE(pads.release("b").value() == 1);
    REQUIRE(pads.assign("d").value() == 1);
}

TEST_CASE("PadSlots refuses more than the host ceiling", "[moonlight][padslots]") {
    PadSlots pads;
    for (std::uint8_t i = 0; i < kMaxPads; ++i) {
        REQUIRE(pads.assign("pad" + std::to_string(i)).has_value());
    }
    REQUIRE_FALSE(pads.assign("one-too-many").has_value());
    REQUIRE(pads.size() == kMaxPads);
}

TEST_CASE("activeMask carries a bit per present pad, and drops it on release",
          "[moonlight][padslots]") {
    PadSlots pads;
    REQUIRE(pads.activeMask() == 0);
    pads.assign("a"); // 0
    REQUIRE(pads.activeMask() == 0x0001);
    pads.assign("b"); // 1
    REQUIRE(pads.activeMask() == 0x0003);
    pads.assign("c"); // 2
    REQUIRE(pads.activeMask() == 0x0007);

    // The unplug signal: b's bit clears while a and c stay present.
    pads.release("b");
    REQUIRE(pads.activeMask() == 0x0005);
    REQUIRE_FALSE(pads.numberFor("b").has_value());

    pads.release("a");
    pads.release("c");
    REQUIRE(pads.activeMask() == 0);
    REQUIRE(pads.empty());
}

TEST_CASE("padCapabilities folds only the hardware that is present", "[moonlight][padslots]") {
    // Analog triggers are always advertised; nothing else without hardware.
    REQUIRE(padCapabilities(false, false, false, false, false) == kPadCapAnalogTriggers);

    const std::uint8_t full = padCapabilities(true, true, true, true, true);
    REQUIRE((full & kPadCapRumble) != 0);
    REQUIRE((full & kPadCapAccel) != 0);
    REQUIRE((full & kPadCapGyro) != 0);
    REQUIRE((full & kPadCapTouchpad) != 0);
    REQUIRE((full & kPadCapBattery) != 0);
    REQUIRE((full & kPadCapRgbLed) != 0);

    // Motion advertises BOTH sensors, since one MOTION_EVENT selects each.
    const std::uint8_t motionOnly = padCapabilities(false, true, false, false, false);
    REQUIRE((motionOnly & (kPadCapAccel | kPadCapGyro)) == (kPadCapAccel | kPadCapGyro));
    REQUIRE((motionOnly & kPadCapRumble) == 0);
}

TEST_CASE("arrivalTypeFromDevicePick maps the picker onto the wire type", "[moonlight][padslots]") {
    REQUIRE(arrivalTypeFromDevicePick(0) == kPadTypeUnknown); // Auto: host decides
    REQUIRE(arrivalTypeFromDevicePick(1) == kPadTypeXbox);
    REQUIRE(arrivalTypeFromDevicePick(2) == kPadTypePlayStation);
    REQUIRE(arrivalTypeFromDevicePick(3) == kPadTypeNintendo);
    // An unknown pick degrades to Auto rather than guessing a pad.
    REQUIRE(arrivalTypeFromDevicePick(99) == kPadTypeUnknown);
}

TEST_CASE("the XUSB button word is bit-identical to Moonlight's low flags",
          "[moonlight][padslots]") {
    // The routing seam forwards the processor's button word to CONTROLLER_MULTI
    // verbatim. That is only correct because both follow the XInput layout; this
    // pins it, so a future renumber on either side fails here instead of
    // silently sending the wrong buttons.
    using B = dish::input::GamepadInputProcessor::Buttons;
    REQUIRE(static_cast<std::uint32_t>(B::kDpadUp) == kBtnDpadUp);
    REQUIRE(static_cast<std::uint32_t>(B::kDpadDown) == kBtnDpadDown);
    REQUIRE(static_cast<std::uint32_t>(B::kDpadLeft) == kBtnDpadLeft);
    REQUIRE(static_cast<std::uint32_t>(B::kDpadRight) == kBtnDpadRight);
    REQUIRE(static_cast<std::uint32_t>(B::kStart) == kBtnStart);
    REQUIRE(static_cast<std::uint32_t>(B::kBack) == kBtnBack);
    REQUIRE(static_cast<std::uint32_t>(B::kLeftThumb) == kBtnLeftStick);
    REQUIRE(static_cast<std::uint32_t>(B::kRightThumb) == kBtnRightStick);
    REQUIRE(static_cast<std::uint32_t>(B::kLeftShoulder) == kBtnLeftButton);
    REQUIRE(static_cast<std::uint32_t>(B::kRightShoulder) == kBtnRightButton);
    REQUIRE(static_cast<std::uint32_t>(B::kA) == kBtnA);
    REQUIRE(static_cast<std::uint32_t>(B::kB) == kBtnB);
    REQUIRE(static_cast<std::uint32_t>(B::kX) == kBtnX);
    REQUIRE(static_cast<std::uint32_t>(B::kY) == kBtnY);
}

TEST_CASE("batteryStateFromSatelliteStatus maps every satellite status", "[moonlight][padslots]") {
    REQUIRE(batteryStateFromSatelliteStatus(0) == kBatteryStateUnknown);
    REQUIRE(batteryStateFromSatelliteStatus(1) == kBatteryStateDischarging);
    REQUIRE(batteryStateFromSatelliteStatus(2) == kBatteryStateCharging);
    REQUIRE(batteryStateFromSatelliteStatus(3) == kBatteryStateFull);
    // Wired is the closest thing the protocol has to charging.
    REQUIRE(batteryStateFromSatelliteStatus(4) == kBatteryStateCharging);
    REQUIRE(batteryStateFromSatelliteStatus(200) == kBatteryStateUnknown);
}
