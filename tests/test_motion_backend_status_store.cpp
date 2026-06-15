// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SatelliteMotionBackendStatusStoreTest (PURE, 13). Port of dish-android
// source/store/SatelliteMotionBackendStatusStoreTest: the SatelliteMotionBackendStatus
// flag-byte decode (FLAG_SINK_SUPPORTED_FOR_TYPE / FLAG_BACKEND_OK, reserved
// upper bits ignored) and the per-(conn,slot) StateSource (round-trip, in-place
// overwrite, selective vs bulk clear, slotStatusesFor projection). The android
// "no spurious emission" assert (reference equality of the StateFlow value) is
// re-expressed here as "a no-op clear() notifies no subscriber" via an
// Observable subscription counter.

#include "source/store/SatelliteMotionBackendStatusStore.h"

#include <catch2/catch_test_macros.hpp>

using dish::source::SatelliteMotionBackendStatus;
using dish::source::SatelliteMotionBackendStatusStore;

TEST_CASE("fromFlags decodes both bits clear", "[motion-backend]") {
    const auto s = SatelliteMotionBackendStatus::fromFlags(0);
    CHECK_FALSE(s.sinkSupportedForType);
    CHECK_FALSE(s.backendOk);
    CHECK_FALSE(s.effective());
}

TEST_CASE("fromFlags decodes only SINK_SUPPORTED_FOR_TYPE", "[motion-backend]") {
    const auto s = SatelliteMotionBackendStatus::fromFlags(
        SatelliteMotionBackendStatus::kFlagSinkSupportedForType);
    CHECK(s.sinkSupportedForType);
    CHECK_FALSE(s.backendOk);
    CHECK_FALSE(s.effective());
}

TEST_CASE("fromFlags decodes only BACKEND_OK", "[motion-backend]") {
    const auto s =
        SatelliteMotionBackendStatus::fromFlags(SatelliteMotionBackendStatus::kFlagBackendOk);
    CHECK_FALSE(s.sinkSupportedForType);
    CHECK(s.backendOk);
    CHECK_FALSE(s.effective());
}

TEST_CASE("fromFlags decodes both bits set", "[motion-backend]") {
    const auto s = SatelliteMotionBackendStatus::fromFlags(
        SatelliteMotionBackendStatus::kFlagSinkSupportedForType |
        SatelliteMotionBackendStatus::kFlagBackendOk);
    CHECK(s.sinkSupportedForType);
    CHECK(s.backendOk);
    CHECK(s.effective());
}

TEST_CASE("fromFlags ignores reserved upper bits", "[motion-backend]") {
    const auto s = SatelliteMotionBackendStatus::fromFlags(0xFF);
    CHECK(s.sinkSupportedForType);
    CHECK(s.backendOk);
}

TEST_CASE("statusFor is null on a fresh store", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    CHECK_FALSE(store.statusFor("conn", "slot").has_value());
}

TEST_CASE("setStatus then statusFor round-trips", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    const SatelliteMotionBackendStatus s{true, false};
    store.setStatus("conn", "slot", s);
    REQUIRE(store.statusFor("conn", "slot").has_value());
    CHECK(*store.statusFor("conn", "slot") == s);
}

TEST_CASE("setStatus overwrites in place", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    store.setStatus("conn", "slot", SatelliteMotionBackendStatus{true, true});
    store.setStatus("conn", "slot", SatelliteMotionBackendStatus{true, false});
    CHECK(*store.statusFor("conn", "slot") == SatelliteMotionBackendStatus{true, false});
}

TEST_CASE("clear drops only the named entry", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    store.setStatus("conn", "slot1", SatelliteMotionBackendStatus{true, true});
    store.setStatus("conn", "slot2", SatelliteMotionBackendStatus{false, false});
    store.clear("conn", "slot1");
    CHECK_FALSE(store.statusFor("conn", "slot1").has_value());
    CHECK(*store.statusFor("conn", "slot2") == SatelliteMotionBackendStatus{false, false});
}

TEST_CASE("clear on a missing entry notifies no subscriber", "[motion-backend]") {
    // The android test pins reference-equality of the StateFlow value to prove a
    // no-op clear() does NOT re-emit (which would re-trigger every collector).
    // The C++ Observable is distinct-until-changed, so we assert it directly: a
    // subscriber wired with emitCurrent=false sees zero notifications.
    SatelliteMotionBackendStatusStore store;
    int notifications = 0;
    auto sub =
        store.state().subscribe([&](const auto&) { ++notifications; }, /*emitCurrent=*/false);
    store.clear("conn", "missing");
    CHECK(notifications == 0);
}

TEST_CASE("clearConnection drops every slot for that connection only", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    store.setStatus("connA", "slot1", SatelliteMotionBackendStatus{true, true});
    store.setStatus("connA", "slot2", SatelliteMotionBackendStatus{true, false});
    store.setStatus("connB", "slot1", SatelliteMotionBackendStatus{false, true});

    store.clearConnection("connA");

    CHECK_FALSE(store.statusFor("connA", "slot1").has_value());
    CHECK_FALSE(store.statusFor("connA", "slot2").has_value());
    CHECK(*store.statusFor("connB", "slot1") == SatelliteMotionBackendStatus{false, true});
}

TEST_CASE("slotStatusesFor returns only the bound slots", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    store.setStatus("conn", "slot1", SatelliteMotionBackendStatus{true, true});
    store.setStatus("conn", "slot2", SatelliteMotionBackendStatus{true, false});
    store.setStatus("conn", "slot3", SatelliteMotionBackendStatus{false, true});
    store.setStatus("other", "slot1", SatelliteMotionBackendStatus{false, false});

    const auto projection = store.slotStatusesFor("conn", {"slot1", "slot2"});

    CHECK(projection.size() == 2);
    CHECK(projection.at("slot1") == SatelliteMotionBackendStatus{true, true});
    CHECK(projection.at("slot2") == SatelliteMotionBackendStatus{true, false});
}

TEST_CASE("slotStatusesFor skips slots with no status entry", "[motion-backend]") {
    SatelliteMotionBackendStatusStore store;
    store.setStatus("conn", "slot1", SatelliteMotionBackendStatus{true, true});

    const auto projection = store.slotStatusesFor("conn", {"slot1", "slot2"});
    CHECK(projection.size() == 1);
    CHECK(projection.find("slot2") == projection.end());
}
