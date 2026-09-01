// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The satellite-to-Moonlight unit translation, and the host's motion
// subscription gate.
//
// The units matter more than they look: both wires carry "a gyro sample", but
// one is fixed-point at a declared full scale and the other is a float in a
// physical unit, and accel additionally changes unit (g to m/s^2). A conversion
// that drops the gravity factor under-reports by 9.8x, which a host reads as a
// pad lying perfectly still however hard it is shaken.

#include "core/moonlight/MoonlightControl.h"
#include "core/moonlight/MoonlightTelemetry.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using Catch::Approx;
using dish::moonlight::accelMs2;
using dish::moonlight::gyroDegS;
using dish::moonlight::kStandardGravity;
using dish::moonlight::MoonlightMotionGate;
using dish::moonlight::touchNorm;

namespace moon = dish::moonlight;

TEST_CASE("gyro converts from the satellite's full scale to deg/s", "[moonlight][telemetry]") {
    // Full scale is +/-2000 deg/s over int16, so the extremes are the check that
    // matters; a wrong divisor shows up there first.
    CHECK(gyroDegS(0) == Approx(0.0F));
    CHECK(gyroDegS(32767) == Approx(2000.0F));
    CHECK(gyroDegS(-32767) == Approx(-2000.0F));
    CHECK(gyroDegS(16384) == Approx(1000.03F).margin(0.05));
}

TEST_CASE("accel converts to metres per second squared, not g", "[moonlight][telemetry]") {
    // +/-4 g full scale, and the wire wants m/s^2. The gravity factor is the
    // whole point of this case.
    CHECK(accelMs2(0) == Approx(0.0F));
    CHECK(accelMs2(32767) == Approx(4.0F * kStandardGravity));
    CHECK(accelMs2(-32767) == Approx(-4.0F * kStandardGravity));
    // One g of gravity, as a pad resting flat reports it.
    CHECK(accelMs2(8192) == Approx(kStandardGravity).margin(0.01));
    // Explicitly NOT the g value: this is the regression that reads as a dead
    // sensor rather than as an error.
    CHECK(accelMs2(32767) != Approx(4.0F));
}

TEST_CASE("touch coordinates normalise to a closed 0..1", "[moonlight][telemetry]") {
    // Both ends have to be exact: a finger at the pad's edge landing at 0.5
    // would put every gesture in the middle of the host's touchpad.
    CHECK(touchNorm(-32768) == Approx(0.0F));
    CHECK(touchNorm(32767) == Approx(1.0F));
    CHECK(touchNorm(0) == Approx(0.5F).margin(0.0001));
}

TEST_CASE("motion stays off until the host asks", "[moonlight][motiongate]") {
    // The default is what keeps an unasked-for IMU stream off the wire. A gate
    // that defaulted open would stream to every host that never wanted it.
    MoonlightMotionGate gate;
    CHECK_FALSE(gate.wanted(0, moon::kMotionGyro));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 0));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionAccel, 1000000));
}

TEST_CASE("a subscription opens exactly one pad and one motion type", "[moonlight][motiongate]") {
    // Sunshine subscribes per type: a game that opened the gyro must not start
    // receiving accel, and pad 0's subscription is not pad 1's.
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    CHECK(gate.wanted(0, moon::kMotionGyro));
    CHECK_FALSE(gate.wanted(0, moon::kMotionAccel));
    CHECK_FALSE(gate.wanted(1, moon::kMotionGyro));
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 0));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionAccel, 0));
    CHECK_FALSE(gate.shouldSend(1, moon::kMotionGyro, 0));
}

TEST_CASE("samples faster than the requested rate are dropped", "[moonlight][motiongate]") {
    // 100 Hz is a 10 ms floor. A pad polling at 250 Hz hands over a sample every
    // 4 ms, and the extra ones are dropped rather than queued: they are stale by
    // the time the host would read them.
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 0));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 4000));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 9999));
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 10000));
    // ...and the window restarts from the accepted sample, not from the clock.
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 15000));
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 20000));
}

TEST_CASE("each type keeps its own cadence", "[moonlight][motiongate]") {
    // A shared timestamp would let a gyro sample consume the accel budget, so a
    // host asking for both would receive half of each.
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    gate.onMotionRequest(0, 100, moon::kMotionAccel);
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 0));
    CHECK(gate.shouldSend(0, moon::kMotionAccel, 0));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 1000));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionAccel, 1000));
}

TEST_CASE("different rates give different floors", "[moonlight][motiongate]") {
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 200, moon::kMotionGyro); // 5 ms
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 0));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 4999));
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 5000));
}

TEST_CASE("rate 0 is how a host says stop", "[moonlight][motiongate]") {
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    REQUIRE(gate.shouldSend(0, moon::kMotionGyro, 0));
    gate.onMotionRequest(0, 0, moon::kMotionGyro);
    CHECK_FALSE(gate.wanted(0, moon::kMotionGyro));
    CHECK_FALSE(gate.shouldSend(0, moon::kMotionGyro, 1000000));
}

TEST_CASE("an unsubscribe clears the cadence too", "[moonlight][motiongate]") {
    // Otherwise a re-subscribe would inherit the old timestamp and drop its
    // first sample for no reason.
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    REQUIRE(gate.shouldSend(0, moon::kMotionGyro, 50000));
    gate.onMotionRequest(0, 0, moon::kMotionGyro);
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    CHECK(gate.shouldSend(0, moon::kMotionGyro, 50001));
}

TEST_CASE("a negative rate is treated as a stop", "[moonlight][motiongate]") {
    // Nothing on the wire should produce one, but a negative interval would
    // divide into a nonsense floor rather than failing loudly.
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    gate.onMotionRequest(0, -5, moon::kMotionGyro);
    CHECK_FALSE(gate.wanted(0, moon::kMotionGyro));
}

TEST_CASE("clearing one pad leaves the others subscribed", "[moonlight][motiongate]") {
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    gate.onMotionRequest(0, 100, moon::kMotionAccel);
    gate.onMotionRequest(1, 100, moon::kMotionGyro);
    gate.clear(0);
    CHECK_FALSE(gate.wanted(0, moon::kMotionGyro));
    CHECK_FALSE(gate.wanted(0, moon::kMotionAccel));
    CHECK(gate.wanted(1, moon::kMotionGyro));
}

TEST_CASE("a returning pad waits to be asked again", "[moonlight][motiongate]") {
    // clear() on unbind is what stops a re-bound pad resuming a stream the host
    // has forgotten it ever requested.
    MoonlightMotionGate gate;
    gate.onMotionRequest(2, 100, moon::kMotionGyro);
    gate.clear(2);
    CHECK_FALSE(gate.shouldSend(2, moon::kMotionGyro, 0));
    gate.onMotionRequest(2, 100, moon::kMotionGyro);
    CHECK(gate.shouldSend(2, moon::kMotionGyro, 0));
}

TEST_CASE("clearAll drops every subscription", "[moonlight][motiongate]") {
    MoonlightMotionGate gate;
    gate.onMotionRequest(0, 100, moon::kMotionGyro);
    gate.onMotionRequest(1, 100, moon::kMotionAccel);
    gate.clearAll();
    CHECK_FALSE(gate.wanted(0, moon::kMotionGyro));
    CHECK_FALSE(gate.wanted(1, moon::kMotionAccel));
}
