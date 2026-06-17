// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotPathFields — the PURE slot-identity x controllers-map -> path-fields
// mapper that AppModel::rebuild() stamps onto each slot, and the synthetic
// slot-id (vpKey) parse the view model resolves a slot's (vid, pid) through.
// These pin the cross-reference WITHOUT a live UsbGamepadManager (which opens
// real USB/SDL and would hang): a routed device reflects its controller's
// phase/desired; a synthetic reflects its own Direct entry; a pad with no
// controller (an Xbox/XInput pad the gateway never enumerates) is unsupported.

#include "core/reducer/SlotPathFields.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using namespace dish::reducer;

namespace {

// One tracked controller entry, as controllers() would return it.
UsbController controller(int vid, int pid, UsbPhase phase, PathChoice desired) {
    UsbController c;
    c.vendorId = vid;
    c.productId = pid;
    c.phase = phase;
    c.desired = desired;
    return c;
}

std::map<int, UsbController> mapOf(const UsbController& c) {
    return {{slotPathVpKey(c.vendorId, c.productId), c}};
}

} // namespace

TEST_CASE("slotPathFields: a routed device reflects its controller's phase + desired",
          "[slotpath][map]") {
    const auto controllers =
        mapOf(controller(0x054C, 0x0CE6, UsbPhase::Routed, PathChoice::Standard));
    const auto f = slotPathFields(0x054C, 0x0CE6, controllers);
    REQUIRE(f.supported);
    REQUIRE(f.phase == UsbPhase::Routed);
    REQUIRE(f.desired == PathChoice::Standard);
    REQUIRE_FALSE(f.failure.has_value());
}

TEST_CASE("slotPathFields: a synthetic (Direct) device reports Direct phase + desired",
          "[slotpath][map]") {
    const auto controllers =
        mapOf(controller(0x054C, 0x0CE6, UsbPhase::Direct, PathChoice::Direct));
    const auto f = slotPathFields(0x054C, 0x0CE6, controllers);
    REQUIRE(f.supported);
    REQUIRE(f.phase == UsbPhase::Direct);
    REQUIRE(f.desired == PathChoice::Direct);
}

TEST_CASE("slotPathFields: a pad with no controller is unsupported (Xbox/XInput)",
          "[slotpath][map]") {
    // The controllers map has a DualSense, but the slot is an Xbox pad (a
    // different vid/pid the raw-HID gateway never enumerates) -> no entry.
    const auto controllers =
        mapOf(controller(0x054C, 0x0CE6, UsbPhase::Direct, PathChoice::Direct));
    const auto f = slotPathFields(0x045E, 0x028E, controllers); // Xbox 360 pad
    REQUIRE_FALSE(f.supported);
    // Inert defaults so the QML hides the control.
    REQUIRE(f.phase == UsbPhase::Routed);
    REQUIRE(f.desired == PathChoice::Standard);
    REQUIRE_FALSE(f.failure.has_value());
}

TEST_CASE("slotPathFields: a 0/0 identity never matches (identity-less SDL slot)",
          "[slotpath][map]") {
    auto c = controller(0, 0, UsbPhase::Direct, PathChoice::Direct);
    std::map<int, UsbController> controllers = {{slotPathVpKey(0, 0), c}};
    REQUIRE_FALSE(slotPathFields(0, 0, controllers).supported);
}

TEST_CASE("slotPathFields: a Direct failure is carried through for the inline note",
          "[slotpath][map]") {
    auto c = controller(0x054C, 0x0CE6, UsbPhase::Routed, PathChoice::Direct);
    c.failure = DirectClaimFailure::Busy;
    const auto f = slotPathFields(0x054C, 0x0CE6, mapOf(c));
    REQUIRE(f.supported);
    REQUIRE(f.failure.has_value());
    REQUIRE(*f.failure == DirectClaimFailure::Busy);
}

TEST_CASE("parseSyntheticSlotId: a packed vpKey string round-trips to (vid, pid)",
          "[slotpath][resolve]") {
    // The synthetic slot id is std::to_string(vpKey); parsing it must recover the
    // exact (vid, pid) the controllers map is keyed on.
    const int vid = 0x054C;
    const int pid = 0x0CE6;
    const int key = slotPathVpKey(vid, pid);
    const auto parsed = parseSyntheticSlotId(std::to_string(key));
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->first == vid);
    REQUIRE(parsed->second == pid);
}

TEST_CASE("parseSyntheticSlotId: an SDL slot id (or junk) does not parse", "[slotpath][resolve]") {
    // An SDL id is "sdl:<iid>" — never an all-digit string, so it falls through
    // to the bridge-device lookup the view model does instead.
    REQUIRE_FALSE(parseSyntheticSlotId("sdl:3").has_value());
    REQUIRE_FALSE(parseSyntheticSlotId("").has_value());
    REQUIRE_FALSE(parseSyntheticSlotId("-5").has_value());
    REQUIRE_FALSE(parseSyntheticSlotId("12abc").has_value());
    // A value past the 32-bit packed range is rejected (never a real key).
    REQUIRE_FALSE(parseSyntheticSlotId("99999999999").has_value());
}

TEST_CASE("parseSyntheticSlotId: a low-vid/pid key still splits correctly", "[slotpath][resolve]") {
    // vid 0x0001, pid 0x0002 -> key 0x00010002 -> "65538".
    const auto parsed = parseSyntheticSlotId("65538");
    REQUIRE(parsed.has_value());
    REQUIRE(parsed->first == 0x0001);
    REQUIRE(parsed->second == 0x0002);
}

TEST_CASE("slotPathSwitching: the derived loading state over the full switch flow",
          "[slotpath][switching]") {
    // Transitional FSM phases are always switching, regardless of desired/form.
    CHECK(slotPathSwitching(UsbPhase::Claiming, PathChoice::Direct, false, 0, false));
    CHECK(slotPathSwitching(UsbPhase::AwaitingFramework, PathChoice::Standard, true, 0, false));

    // Want Direct: switching until a synthetic exists AND its poll rate is measured
    // (the observed "Hz comes in a moment later" window).
    CHECK(slotPathSwitching(UsbPhase::Direct, PathChoice::Direct, false, 0,
                            false)); // not synthetic yet
    CHECK(slotPathSwitching(UsbPhase::Direct, PathChoice::Direct, true, 0,
                            false)); // synthetic, no rate yet
    CHECK_FALSE(slotPathSwitching(UsbPhase::Direct, PathChoice::Direct, true, 250,
                                  false)); // settled + streaming

    // Want Standard: switching while still on the synthetic; settled once back on SDL.
    CHECK(slotPathSwitching(UsbPhase::Routed, PathChoice::Standard, true, 100,
                            false)); // still synthetic
    CHECK_FALSE(
        slotPathSwitching(UsbPhase::Routed, PathChoice::Standard, false, 0, false)); // back on SDL

    // A settled Standard slot at rest is never switching.
    CHECK_FALSE(slotPathSwitching(UsbPhase::Routed, PathChoice::Standard, false, 0, false));

    // Terminal / failure states surface an error note, never a perpetual spinner.
    CHECK_FALSE(slotPathSwitching(UsbPhase::RestoreStuck, PathChoice::Direct, false, 0, false));
    CHECK_FALSE(slotPathSwitching(UsbPhase::NeedsReplug, PathChoice::Direct, false, 0, false));
    CHECK_FALSE(
        slotPathSwitching(UsbPhase::Routed, PathChoice::Direct, false, 0, /*hasFailure=*/true));
}
