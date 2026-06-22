// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Coverage for the RAW-JOYSTICK fallback mapper in JoystickMapping.{h,cpp}.
// Generic pads SDL classifies as a joystick but NOT a game controller (no entry
// in SDL's mapping DB) have no normalised layout — this maps a raw axis/button/
// hat snapshot to the GamepadInputProcessor::DeviceState the processor consumes,
// using a best-guess default DirectInput layout. The mapper is pure / SDL-free
// precisely so it can be exercised here without opening a device. Mirrors the
// style of test_sdl_motion_convert.cpp.
//
// Default layout under test (see JoystickMapping.h):
//   axes 0/1 = left stick X/Y; right stick = 3/4 (6-axis pads, triggers on 2/5)
//   or 2/3 (4/5-axis pads, triggers from buttons); buttons 0-3 = A/B/X/Y,
//   4/5 = shoulders, 6/7 = back/start, 10/11 = stick clicks; hat 0 → dpad.
//   Y axes are inverted (SDL +down → report +up).

#include "Input/JoystickMapping.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>

using dish::input::axisAt;
using dish::input::buttonAt;
using dish::input::hasTriggerAxes;
using dish::input::JoystickRemap;
using dish::input::JoystickSnapshot;
using dish::input::mapJoystick;
using dish::input::RemapButton;
using dish::input::triggerFromAxis;
using dish::input::TriggerSource;
using dish::input::TriggerSourceKind;
using B = dish::input::GamepadInputProcessor::Buttons;
namespace hat = dish::input::hat;

namespace {

// Build a snapshot over caller-owned arrays. The arrays must outlive the
// snapshot (and the mapJoystick call) — every test below keeps them in scope.
JoystickSnapshot makeSnapshot(const std::int16_t* axes, int axisCount, const bool* buttons,
                              int buttonCount, const std::uint8_t* hats, int hatCount) {
    JoystickSnapshot s{};
    s.axes = axes;
    s.axisCount = axisCount;
    s.buttons = buttons;
    s.buttonCount = buttonCount;
    s.hats = hats;
    s.hatCount = hatCount;
    return s;
}

} // namespace

// ── Safe accessors: out-of-range / null must not crash or misread ────────────

TEST_CASE("axisAt returns 0 for out-of-range and null", "[joymap]") {
    std::array<std::int16_t, 2> axes{100, 200};
    JoystickSnapshot s = makeSnapshot(axes.data(), 2, nullptr, 0, nullptr, 0);
    REQUIRE(axisAt(s, 0) == 100);
    REQUIRE(axisAt(s, 1) == 200);
    // Past the real count → neutral, not a heap over-read.
    REQUIRE(axisAt(s, 2) == 0);
    REQUIRE(axisAt(s, 99) == 0);
    REQUIRE(axisAt(s, -1) == 0);
    // Null array with a (lying) non-zero count still reads neutral.
    JoystickSnapshot bad = makeSnapshot(nullptr, 4, nullptr, 0, nullptr, 0);
    REQUIRE(axisAt(bad, 0) == 0);
}

TEST_CASE("buttonAt returns false for out-of-range and null", "[joymap]") {
    std::array<bool, 2> buttons{true, false};
    JoystickSnapshot s = makeSnapshot(nullptr, 0, buttons.data(), 2, nullptr, 0);
    REQUIRE(buttonAt(s, 0) == true);
    REQUIRE(buttonAt(s, 1) == false);
    REQUIRE(buttonAt(s, 2) == false);
    REQUIRE(buttonAt(s, 99) == false);
    REQUIRE(buttonAt(s, -1) == false);
    JoystickSnapshot bad = makeSnapshot(nullptr, 0, nullptr, 16, nullptr, 0);
    REQUIRE(buttonAt(bad, 0) == false);
}

// ── triggerFromAxis ──────────────────────────────────────────────────────────

TEST_CASE("triggerFromAxis maps the positive int16 half to 0..255", "[joymap]") {
    REQUIRE(triggerFromAxis(0) == 0);
    REQUIRE(triggerFromAxis(-1) == 0);
    REQUIRE(triggerFromAxis(-32768) == 0);
    REQUIRE(triggerFromAxis(32767) == 255);
    // Half of positive full scale ≈ 127.
    REQUIRE(triggerFromAxis(16384) == 127);
}

// ── hasTriggerAxes ───────────────────────────────────────────────────────────

TEST_CASE("hasTriggerAxes is true only with >= 6 axes", "[joymap]") {
    std::array<std::int16_t, 8> a{};
    REQUIRE(hasTriggerAxes(makeSnapshot(a.data(), 6, nullptr, 0, nullptr, 0)) == true);
    REQUIRE(hasTriggerAxes(makeSnapshot(a.data(), 8, nullptr, 0, nullptr, 0)) == true);
    REQUIRE(hasTriggerAxes(makeSnapshot(a.data(), 5, nullptr, 0, nullptr, 0)) == false);
    REQUIRE(hasTriggerAxes(makeSnapshot(a.data(), 4, nullptr, 0, nullptr, 0)) == false);
    REQUIRE(hasTriggerAxes(makeSnapshot(a.data(), 2, nullptr, 0, nullptr, 0)) == false);
}

// ── Stick routing: 6-axis pad (right stick on 3/4, triggers on 2/5) ─────────

TEST_CASE("6-axis pad routes left 0/1, right 3/4 with Y inverted", "[joymap]") {
    // axis: 0=lx 1=ly 2=lt 3=rx 4=ry 5=rt
    std::array<std::int16_t, 6> axes{1000, 2000, 0, 3000, 4000, 0};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, nullptr, 0, nullptr, 0));
    REQUIRE(st.lx == 1000);
    REQUIRE(st.ly == -2000); // inverted
    REQUIRE(st.rx == 3000);
    REQUIRE(st.ry == -4000); // inverted
}

TEST_CASE("6-axis pad sources triggers from axes 2 and 5", "[joymap]") {
    std::array<std::int16_t, 6> axes{0, 0, 32767, 0, 0, 16384};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, nullptr, 0, nullptr, 0));
    REQUIRE(st.lt == 255);
    REQUIRE(st.rt == 127);
}

// ── Stick routing: 4-axis pad (right stick on 2/3, triggers from buttons) ───

TEST_CASE("4-axis pad routes right stick to axes 2/3", "[joymap]") {
    // axis: 0=lx 1=ly 2=rx 3=ry
    std::array<std::int16_t, 4> axes{1000, 2000, 3000, 4000};
    auto st = mapJoystick(makeSnapshot(axes.data(), 4, nullptr, 0, nullptr, 0));
    REQUIRE(st.lx == 1000);
    REQUIRE(st.ly == -2000);
    REQUIRE(st.rx == 3000);
    REQUIRE(st.ry == -4000);
}

TEST_CASE("4-axis pad sources triggers from buttons 8/9 (no trigger axes)", "[joymap]") {
    std::array<std::int16_t, 4> axes{0, 0, 0, 0};
    std::array<bool, 10> buttons{};
    buttons[8] = true; // left trigger button
    auto st = mapJoystick(makeSnapshot(axes.data(), 4, buttons.data(), 10, nullptr, 0));
    REQUIRE(st.lt == 255);
    REQUIRE(st.rt == 0);
    buttons[9] = true; // right trigger button
    st = mapJoystick(makeSnapshot(axes.data(), 4, buttons.data(), 10, nullptr, 0));
    REQUIRE(st.lt == 255);
    REQUIRE(st.rt == 255);
}

// ── Face / shoulder / system / stick-click buttons ──────────────────────────

TEST_CASE("face buttons 0-3 map A/B/X/Y", "[joymap]") {
    std::array<bool, 4> buttons{};
    buttons[0] = true;
    auto st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 4, nullptr, 0));
    REQUIRE((st.wButtons & B::kA) != 0);
    REQUIRE((st.wButtons & (B::kB | B::kX | B::kY)) == 0);

    buttons = {false, true, false, false};
    st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 4, nullptr, 0));
    REQUIRE((st.wButtons & B::kB) != 0);

    buttons = {false, false, true, false};
    st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 4, nullptr, 0));
    REQUIRE((st.wButtons & B::kX) != 0);

    buttons = {false, false, false, true};
    st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 4, nullptr, 0));
    REQUIRE((st.wButtons & B::kY) != 0);
}

TEST_CASE("shoulders 4/5, back/start 6/7, stick clicks 10/11", "[joymap]") {
    std::array<bool, 12> buttons{};
    buttons[4] = true;
    buttons[5] = true;
    buttons[6] = true;
    buttons[7] = true;
    buttons[10] = true;
    buttons[11] = true;
    auto st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 12, nullptr, 0));
    REQUIRE((st.wButtons & B::kLeftShoulder) != 0);
    REQUIRE((st.wButtons & B::kRightShoulder) != 0);
    REQUIRE((st.wButtons & B::kBack) != 0);
    REQUIRE((st.wButtons & B::kStart) != 0);
    REQUIRE((st.wButtons & B::kLeftThumb) != 0);
    REQUIRE((st.wButtons & B::kRightThumb) != 0);
}

TEST_CASE("button 8 (guide) maps to no XUSB bit", "[joymap]") {
    // The XUSB report the processor consumes has no guide bit; pressing the
    // guide key alone must not light any button (and must not alias Start/Back).
    std::array<bool, 9> buttons{};
    buttons[8] = true;
    auto st = mapJoystick(makeSnapshot(nullptr, 0, buttons.data(), 9, nullptr, 0));
    // With >= 6 axes the trigger-from-button path is off, so button 8 has no
    // effect at all. Provide 6 axes so triggers come from axes, isolating 8.
    std::array<std::int16_t, 6> axes{};
    st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 9, nullptr, 0));
    REQUIRE(st.wButtons == 0);
}

// ── Hat → dpad: all 8 directions ─────────────────────────────────────────────

TEST_CASE("hat maps all 8 directions to dpad bits", "[joymap]") {
    auto dpadFor = [](std::uint8_t h) -> std::uint16_t {
        std::array<std::uint8_t, 1> hats{h};
        auto st = mapJoystick(makeSnapshot(nullptr, 0, nullptr, 0, hats.data(), 1));
        return st.wButtons;
    };

    REQUIRE(dpadFor(hat::kCentered) == 0);
    REQUIRE(dpadFor(hat::kUp) == B::kDpadUp);
    REQUIRE(dpadFor(hat::kDown) == B::kDpadDown);
    REQUIRE(dpadFor(hat::kLeft) == B::kDpadLeft);
    REQUIRE(dpadFor(hat::kRight) == B::kDpadRight);
    // Diagonals set two bits (SDL_HAT_* is a bitmask).
    REQUIRE(dpadFor(static_cast<std::uint8_t>(hat::kUp | hat::kRight)) ==
            (B::kDpadUp | B::kDpadRight));
    REQUIRE(dpadFor(static_cast<std::uint8_t>(hat::kUp | hat::kLeft)) ==
            (B::kDpadUp | B::kDpadLeft));
    REQUIRE(dpadFor(static_cast<std::uint8_t>(hat::kDown | hat::kRight)) ==
            (B::kDpadDown | B::kDpadRight));
    REQUIRE(dpadFor(static_cast<std::uint8_t>(hat::kDown | hat::kLeft)) ==
            (B::kDpadDown | B::kDpadLeft));
}

// ── Totality: a sparse pad with fewer axes/buttons than the layout assumes ──

TEST_CASE("a minimal pad (2 axes, 2 buttons, no hat) maps safely", "[joymap]") {
    // A bare pad far smaller than the default layout: only a left stick and two
    // face buttons, no hat. Must produce a sane report with everything the pad
    // lacks reading neutral — no crash, no over-read.
    std::array<std::int16_t, 2> axes{500, -600};
    std::array<bool, 2> buttons{true, false}; // A pressed
    auto st = mapJoystick(makeSnapshot(axes.data(), 2, buttons.data(), 2, nullptr, 0));
    REQUIRE(st.lx == 500);
    REQUIRE(st.ly == 600); // -(-600)
    REQUIRE(st.rx == 0);
    REQUIRE(st.ry == 0);
    REQUIRE(st.lt == 0);
    REQUIRE(st.rt == 0);
    REQUIRE((st.wButtons & B::kA) != 0);
    // No dpad / shoulders / triggers from a pad that lacks them.
    REQUIRE((st.wButtons & ~static_cast<std::uint16_t>(B::kA)) == 0);
}

TEST_CASE("an all-zero snapshot maps to a neutral report", "[joymap]") {
    JoystickSnapshot s{}; // all null / zero counts
    auto st = mapJoystick(s);
    REQUIRE(st.wButtons == 0);
    REQUIRE(st.lx == 0);
    REQUIRE(st.ly == 0);
    REQUIRE(st.rx == 0);
    REQUIRE(st.ry == 0);
    REQUIRE(st.lt == 0);
    REQUIRE(st.rt == 0);
}

// ── DEFAULT-EQUIVALENCE: the parameterized overload under the default remap
//    must equal the legacy overload for the same snapshot ──────────────────────

TEST_CASE("default JoystickRemap matches the legacy mapJoystick overload", "[joymap][remap]") {
    // A snapshot exercising sticks, both trigger paths, faces, shoulders,
    // system buttons, stick clicks, and a hat — fed through BOTH overloads.
    std::array<std::int16_t, 6> axes{1000, -2000, 24000, 3000, -4000, 16384};
    std::array<bool, 12> buttons{true, false, true,  false, true, true,
                                 true, true,  false, false, true, true};
    std::array<std::uint8_t, 1> hats{static_cast<std::uint8_t>(hat::kUp | hat::kRight)};
    auto snap = makeSnapshot(axes.data(), 6, buttons.data(), 12, hats.data(), 1);

    auto legacy = mapJoystick(snap);
    auto viaDefault = mapJoystick(snap, JoystickRemap{});
    REQUIRE(legacy == viaDefault);

    // And the same for a 4-axis pad (the adaptive right-stick + button-trigger
    // fallback path) so default-equivalence covers BOTH branches.
    std::array<std::int16_t, 4> axes4{500, 600, 700, 800};
    std::array<bool, 10> buttons4{};
    buttons4[8] = true; // adaptive left-trigger button
    auto snap4 = makeSnapshot(axes4.data(), 4, buttons4.data(), 10, nullptr, 0);
    REQUIRE(mapJoystick(snap4) == mapJoystick(snap4, JoystickRemap{}));
}

// ── NON-DEFAULT remaps: routing follows the table ───────────────────────────

TEST_CASE("remap swaps A and B", "[joymap][remap]") {
    JoystickRemap remap{};
    // Route logical A from source button 1 and logical B from source button 0.
    remap.buttons[static_cast<int>(RemapButton::A)] = 1;
    remap.buttons[static_cast<int>(RemapButton::B)] = 0;

    std::array<bool, 4> buttons{true, false, false, false}; // physical button 0 down
    std::array<std::int16_t, 6> axes{}; // 6 axes so the trigger-button path is off
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 4, nullptr, 0), remap);
    // Physical 0 now drives B (swapped), not A.
    REQUIRE((st.wButtons & B::kB) != 0);
    REQUIRE((st.wButtons & B::kA) == 0);

    buttons = {false, true, false, false}; // physical button 1 down
    st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 4, nullptr, 0), remap);
    REQUIRE((st.wButtons & B::kA) != 0);
    REQUIRE((st.wButtons & B::kB) == 0);
}

TEST_CASE("remap moves the right stick to axes 2/3 on a 6-axis pad", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.rightStickX = 2;
    remap.rightStickY = 3;
    remap.useAdaptiveRightStick = false; // explicit choice — no adaptive fallback
    // 6 axes so adaptive would NOT trigger anyway; this asserts the explicit
    // 2/3 routing overrides the default 3/4 even with dedicated trigger axes.
    std::array<std::int16_t, 6> axes{0, 0, 1111, 2222, 9999, 9999};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, nullptr, 0, nullptr, 0), remap);
    REQUIRE(st.rx == 1111);
    REQUIRE(st.ry == -2222); // inverted by default
}

TEST_CASE("remap moves a trigger from an axis to a button", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.useAdaptiveTriggers = false; // honour the explicit sources verbatim
    remap.leftTrigger = TriggerSource{TriggerSourceKind::Button, 3};
    // Right trigger stays an axis (default index 5).
    std::array<std::int16_t, 6> axes{0, 0, 0, 0, 0, 32767}; // axis 5 = full right trigger
    std::array<bool, 4> buttons{false, false, false, true}; // button 3 = left trigger
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 4, nullptr, 0), remap);
    REQUIRE(st.lt == 255); // from the button
    REQUIRE(st.rt == 255); // from axis 5
}

TEST_CASE("remap can unassign a button to neutral", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.buttons[static_cast<int>(RemapButton::A)] = -1;   // unassigned
    std::array<bool, 4> buttons{true, false, false, false}; // physical 0 down
    std::array<std::int16_t, 6> axes{};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 4, nullptr, 0), remap);
    REQUIRE((st.wButtons & B::kA) == 0); // unassigned → never lights
}

TEST_CASE("remap can change the hat index", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.hatIndex = 1; // read the SECOND hat
    std::array<std::uint8_t, 2> hats{hat::kCentered, hat::kLeft};
    auto st = mapJoystick(makeSnapshot(nullptr, 0, nullptr, 0, hats.data(), 2), remap);
    REQUIRE((st.wButtons & B::kDpadLeft) != 0);
    REQUIRE((st.wButtons & B::kDpadUp) == 0);

    // hatIndex -1 disables the hat dpad entirely.
    remap.hatIndex = -1;
    st = mapJoystick(makeSnapshot(nullptr, 0, nullptr, 0, hats.data(), 2), remap);
    REQUIRE(st.wButtons == 0);
}

TEST_CASE("remap invert toggles flip stick Y polarity", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.invertLeftY = false;  // pass through
    remap.invertRightY = false; // pass through
    std::array<std::int16_t, 6> axes{0, 2000, 0, 0, 4000, 0};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, nullptr, 0, nullptr, 0), remap);
    REQUIRE(st.ly == 2000); // not negated
    REQUIRE(st.ry == 4000); // not negated

    remap.invertLeftY = true; // back to the historical invert
    st = mapJoystick(makeSnapshot(axes.data(), 6, nullptr, 0, nullptr, 0), remap);
    REQUIRE(st.ly == -2000);
}

TEST_CASE("remap can route a dpad direction to a button", "[joymap][remap]") {
    JoystickRemap remap{};
    remap.buttons[static_cast<int>(RemapButton::DpadUp)] = 6; // dpad-up from button 6
    std::array<bool, 7> buttons{};
    buttons[6] = true;
    std::array<std::int16_t, 6> axes{};
    auto st = mapJoystick(makeSnapshot(axes.data(), 6, buttons.data(), 7, nullptr, 0), remap);
    REQUIRE((st.wButtons & B::kDpadUp) != 0);
}
