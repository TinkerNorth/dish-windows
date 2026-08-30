// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Controller-number allocation, the active mask (whose cleared bit IS the
// unplug signal), the session reference count, the hard-coded per-type
// capability table, the client-side Auto resolution, and the arrival packet the
// three of them build.

#include "Input/GamepadInputProcessor.h"
#include "Network/MoonlightHost.h"
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

TEST_CASE("The session is reference counted per host, not per binding", "[moonlight][padslots]") {
    PadSlots pads;

    // The first pad brings the session up; the three that follow join the one
    // that is already running.
    REQUIRE(bindStartsSession(pads));
    pads.assign("pad0");
    REQUIRE_FALSE(bindStartsSession(pads));
    pads.assign("pad1");
    REQUIRE_FALSE(bindStartsSession(pads));
    pads.assign("pad2");
    REQUIRE_FALSE(bindStartsSession(pads));
    pads.assign("pad3");
    REQUIRE(pads.size() == kMaxPads);

    // Three of the four leave and the session stays up: a /cancel here would
    // strand the app on the host for the pad that is still using it.
    pads.release("pad0");
    REQUIRE_FALSE(unbindEndsSession(pads));
    pads.release("pad2");
    REQUIRE_FALSE(unbindEndsSession(pads));
    pads.release("pad3");
    REQUIRE_FALSE(unbindEndsSession(pads));

    // The last one off owns the teardown.
    pads.release("pad1");
    REQUIRE(unbindEndsSession(pads));
}

TEST_CASE("Auto resolves on the client and never sends Unknown", "[moonlight][padslots]") {
    // Auto is 0xFF rather than 0: 0 is the wire's CONTROLLER_TYPE_UNKNOWN.
    REQUIRE(dish::models::kMoonlightDeviceAuto == 0xFF);
    REQUIRE(arrivalTypeFromDevicePick(dish::models::kMoonlightDeviceAuto) == kPadTypeUnknown);

    // A source with motion asks for a PlayStation pad, everything else for an
    // Xbox one, and the resolution happens before the packet is built.
    REQUIRE(resolveAutoArrivalType(true) == kPadTypePlayStation);
    REQUIRE(resolveAutoArrivalType(false) == kPadTypeXbox);
    REQUIRE(arrivalTypeForBinding(dish::models::kMoonlightDeviceAuto, true) == kPadTypePlayStation);
    REQUIRE(arrivalTypeForBinding(dish::models::kMoonlightDeviceAuto, false) == kPadTypeXbox);

    // An explicit pick is never overridden by what the pad happens to carry.
    REQUIRE(arrivalTypeForBinding(dish::models::kMoonlightDeviceXbox, true) == kPadTypeXbox);
    REQUIRE(arrivalTypeForBinding(dish::models::kMoonlightDeviceNintendo, true) ==
            kPadTypeNintendo);
    REQUIRE(arrivalTypeForBinding(dish::models::kMoonlightDevicePlayStation, false) ==
            kPadTypePlayStation);
}

TEST_CASE("The capability ceiling is the table the reference host builds",
          "[moonlight][padslots]") {
    // Xbox and Nintendo carry analog triggers and rumble and nothing else.
    // NINTENDO HAS NO MOTION over Moonlight, unlike the Satellite type of the
    // same name.
    REQUIRE(typeCapabilityCeiling(kPadTypeXbox) == 0x03);
    REQUIRE(typeCapabilityCeiling(kPadTypeNintendo) == 0x03);
    REQUIRE((typeCapabilityCeiling(kPadTypeNintendo) & kPadCapGyro) == 0);
    REQUIRE((typeCapabilityCeiling(kPadTypeNintendo) & kPadCapAccel) == 0);
    REQUIRE((typeCapabilityCeiling(kPadTypeNintendo) & kPadCapTouchpad) == 0);
    REQUIRE(typeCapabilityCeiling(kPadTypePlayStation) == 0xFF);
}

TEST_CASE("CONTROLLER_ARRIVAL declares the type ceiling met by the pad hardware",
          "[moonlight][padslots]") {
    // A pad with everything, on each type. The declaration never exceeds the
    // ceiling, because a host asked for motion it will not receive keeps asking.
    const std::uint8_t everything =
        declaredCapabilities(kPadTypePlayStation, true, true, true, true, true);
    REQUIRE(everything == (kPadCapAnalogTriggers | kPadCapRumble | kPadCapTouchpad | kPadCapAccel |
                           kPadCapGyro | kPadCapBattery | kPadCapRgbLed));

    REQUIRE(declaredCapabilities(kPadTypeXbox, true, true, true, true, true) == 0x03);
    REQUIRE(declaredCapabilities(kPadTypeNintendo, true, true, true, true, true) == 0x03);

    // And it never exceeds the pad either: a pad with no motors declares none.
    REQUIRE(declaredCapabilities(kPadTypePlayStation, false, false, false, false, false) ==
            kPadCapAnalogTriggers);
    REQUIRE(declaredCapabilities(kPadTypeXbox, false, false, false, false, false) ==
            kPadCapAnalogTriggers);

    // The pad a live host logged as capabilities [0003]: rumble and nothing else.
    REQUIRE(declaredCapabilities(kPadTypeXbox, true, false, false, false, false) == 0x0003);
}

TEST_CASE("The button word is the low sixteen, plus the touchpad click only when there is one",
          "[moonlight][padslots]") {
    // What a live host read back as supportedButtonFlags [0000FFFF].
    REQUIRE(declaredButtonFlags(declaredCapabilities(kPadTypeXbox, true, false, false, false,
                                                     false)) == 0x0000FFFFu);
    REQUIRE(declaredButtonFlags(declaredCapabilities(kPadTypeNintendo, true, true, true, true,
                                                     true)) == 0x0000FFFFu);
    REQUIRE(declaredButtonFlags(declaredCapabilities(kPadTypePlayStation, true, true, true, true,
                                                     true)) == (0x0000FFFFu | 0x00100000u));
    // A PlayStation type on a pad with no touchpad claims no touchpad click.
    REQUIRE(declaredButtonFlags(declaredCapabilities(kPadTypePlayStation, true, true, false, true,
                                                     true)) == 0x0000FFFFu);
}

TEST_CASE("The arrival packet is byte-exact for every type", "[moonlight][padslots]") {
    // The body is EIGHT bytes: the u32 button mask starts at offset 4, so offset
    // 3 is padding. Seven shifted everything after the type by a byte and a live
    // host read capabilities [FF03] for a pad that had announced 0x03.
    struct Case {
        int devicePick;
        bool sourceMotion;
        std::uint8_t type;
        std::uint8_t caps;
        std::uint32_t buttons;
    };
    // The PlayStation ceiling is 0xFF, but trigger rumble (0x04) is not a bit any
    // input source here reports, so the declaration a real pad makes is 0xFB.
    const Case cases[] = {
        {dish::models::kMoonlightDeviceXbox, false, 0x01, 0x03, 0x0000FFFFu},
        {dish::models::kMoonlightDevicePlayStation, true, 0x02, 0xFB, 0x0000FFFFu | 0x00100000u},
        {dish::models::kMoonlightDeviceNintendo, true, 0x03, 0x03, 0x0000FFFFu},
    };

    for (const auto& c : cases) {
        const std::uint8_t type = arrivalTypeForBinding(c.devicePick, c.sourceMotion);
        REQUIRE(type == c.type);
        const std::uint8_t caps =
            declaredCapabilities(type, /*hasRumble=*/true, /*hasMotion=*/c.sourceMotion,
                                 /*hasTouchpad=*/true, /*hasBattery=*/true, /*hasLightbar=*/true);
        REQUIRE(caps == c.caps);
        const std::uint32_t buttons = declaredButtonFlags(caps);
        REQUIRE(buttons == c.buttons);

        // wrapper(12) + body(8), and the body's u32 starts at its offset 4.
        const auto packet = encodeControllerArrival(1, type, caps, buttons);
        REQUIRE(packet.size() == 12 + kControllerArrivalBody);
        REQUIRE(packet[12] == 1);
        REQUIRE(packet[13] == c.type);
        REQUIRE(packet[14] == c.caps);
        REQUIRE(packet[15] == 0); // the alignment pad
        REQUIRE(packet[16] == static_cast<std::uint8_t>(c.buttons & 0xFF));
        REQUIRE(packet[17] == static_cast<std::uint8_t>((c.buttons >> 8) & 0xFF));
        REQUIRE(packet[18] == static_cast<std::uint8_t>((c.buttons >> 16) & 0xFF));
        REQUIRE(packet[19] == static_cast<std::uint8_t>((c.buttons >> 24) & 0xFF));
    }
}

TEST_CASE("Auto sends the resolved type own capability set", "[moonlight][padslots]") {
    // A pad with motion, on Auto: the host is told PlayStation and the motion,
    // touchpad and LED bits its own hardware actually carries.
    const std::uint8_t type =
        arrivalTypeForBinding(dish::models::kMoonlightDeviceAuto, /*sourceHasMotion=*/true);
    REQUIRE(type == kPadTypePlayStation);
    const std::uint8_t caps = declaredCapabilities(type, true, true, true, false, false);
    REQUIRE((caps & kPadCapGyro) != 0);
    REQUIRE((caps & kPadCapAccel) != 0);
    REQUIRE((caps & kPadCapTouchpad) != 0);
    REQUIRE((caps & kPadCapBattery) == 0);

    // The same pad on Auto with no motion is an Xbox pad, whose ceiling drops the
    // touchpad it does have: the type cannot carry one.
    const std::uint8_t plain =
        arrivalTypeForBinding(dish::models::kMoonlightDeviceAuto, /*sourceHasMotion=*/false);
    REQUIRE(plain == kPadTypeXbox);
    REQUIRE(declaredCapabilities(plain, true, false, true, false, false) == 0x03);
}
