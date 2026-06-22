// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbTwinDedupTest — the twin-dedup arbitration (core/reducer/UsbTwinDedup.h),
// a 1:1 port of dish-android ui/main/SyntheticTwinDedupTest.kt plus the
// Windows-specific claim->suppress->detach->restore arbitration the prompt
// requires. Pure; no IO. The reducer answers "which SDL/framework device ids are
// hidden by an active USB-direct synthetic of the same model" so a pad visible to
// both paths streams via exactly one (double input is a bug).

#include "core/reducer/UsbTwinDedup.h"

#include <catch2/catch_test_macros.hpp>

#include <set>
#include <string>
#include <vector>

using dish::reducer::RoutedDevice;
using dish::reducer::suppressedRoutedIds;
using dish::reducer::SyntheticTwin;

namespace {

constexpr int kXboxVid = 0x045E, kXboxPid = 0x028E;
constexpr int kSonyVid = 0x054C, kDs4Pid = 0x05C4;

RoutedDevice routed(const std::string& id, int vid, int pid, bool disconnecting = false) {
    return RoutedDevice{id, vid, pid, disconnecting};
}

SyntheticTwin synthetic(int vid, int pid) { return SyntheticTwin{vid, pid}; }

bool has(const std::set<std::string>& s, const std::string& id) { return s.count(id) != 0; }

} // namespace

TEST_CASE("twin-dedup empty input hides nothing", "[usb-dedup]") {
    CHECK(suppressedRoutedIds({}, {}).empty());
}

TEST_CASE("twin-dedup with no synthetics nothing is hidden", "[usb-dedup]") {
    std::vector<RoutedDevice> devices = {routed("a", kXboxVid, kXboxPid),
                                         routed("b", kSonyVid, kDs4Pid)};
    CHECK(suppressedRoutedIds({}, devices).empty());
}

TEST_CASE("twin-dedup one synthetic hides its single same-model routed twin", "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kSonyVid, kDs4Pid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kSonyVid, kDs4Pid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(hidden.size() == 1);
    CHECK(has(hidden, "sdl:7"));
}

TEST_CASE("twin-dedup a routed controller of a different model stays visible", "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kSonyVid, kDs4Pid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kXboxVid, kXboxPid)};
    CHECK(suppressedRoutedIds(syn, dev).empty());
}

TEST_CASE("twin-dedup Xbox routed pad is never hidden because Xbox never synthesizes",
          "[usb-dedup]") {
    // XInput hides Xbox from raw HID, so an Xbox pad is never a synthetic. A Sony
    // synthetic present alongside an Xbox routed pad leaves the Xbox pad on SDL.
    std::vector<SyntheticTwin> syn = {synthetic(kSonyVid, kDs4Pid)};
    std::vector<RoutedDevice> dev = {routed("sdl:xbox", kXboxVid, kXboxPid),
                                     routed("sdl:ds4", kSonyVid, kDs4Pid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(has(hidden, "sdl:ds4"));
    CHECK_FALSE(has(hidden, "sdl:xbox"));
}

TEST_CASE("twin-dedup two synthetics of one model hide both same-model routed twins",
          "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kXboxVid, kXboxPid), synthetic(kXboxVid, kXboxPid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kXboxVid, kXboxPid),
                                     routed("sdl:8", kXboxVid, kXboxPid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(hidden.size() == 2);
    CHECK(has(hidden, "sdl:7"));
    CHECK(has(hidden, "sdl:8"));
}

TEST_CASE("twin-dedup one synthetic with two routed twins hides the disconnecting one first",
          "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kSonyVid, kDs4Pid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kSonyVid, kDs4Pid, /*disconnecting=*/false),
                                     routed("sdl:8", kSonyVid, kDs4Pid, /*disconnecting=*/true)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(hidden.size() == 1);
    CHECK(has(hidden, "sdl:8")); // the disconnecting ghost is hidden, the live twin survives.
}

TEST_CASE("twin-dedup one synthetic with two live routed twins hides exactly one", "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kSonyVid, kDs4Pid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kSonyVid, kDs4Pid),
                                     routed("sdl:8", kSonyVid, kDs4Pid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(hidden.size() == 1);
    CHECK((has(hidden, "sdl:7") || has(hidden, "sdl:8")));
}

TEST_CASE("twin-dedup more synthetics than routed twins hides every available twin",
          "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kXboxVid, kXboxPid), synthetic(kXboxVid, kXboxPid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kXboxVid, kXboxPid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(hidden.size() == 1);
    CHECK(has(hidden, "sdl:7"));
}

TEST_CASE("twin-dedup synthetic with missing vid or pid hides nothing", "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(0, 0)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", 0, 0)};
    CHECK(suppressedRoutedIds(syn, dev).empty());
}

TEST_CASE("twin-dedup synthetics only hide twins of their own model", "[usb-dedup]") {
    std::vector<SyntheticTwin> syn = {synthetic(kXboxVid, kXboxPid)};
    std::vector<RoutedDevice> dev = {routed("sdl:7", kXboxVid, kXboxPid),
                                     routed("sdl:8", kSonyVid, kDs4Pid)};
    const auto hidden = suppressedRoutedIds(syn, dev);
    CHECK(has(hidden, "sdl:7"));
    CHECK_FALSE(has(hidden, "sdl:8"));
}

// ── claim -> suppress -> detach -> restore (the Windows arbitration lifecycle) ──

TEST_CASE("twin-dedup detach restores the SDL twin", "[usb-dedup]") {
    std::vector<RoutedDevice> dev = {routed("sdl:7", kSonyVid, kDs4Pid)};

    // Claim active: the SDL twin is suppressed.
    std::vector<SyntheticTwin> claimed = {synthetic(kSonyVid, kDs4Pid)};
    CHECK(has(suppressedRoutedIds(claimed, dev), "sdl:7"));

    // Detach / claim-failure: no synthetics -> the SDL twin is visible again.
    CHECK(suppressedRoutedIds({}, dev).empty());
}
