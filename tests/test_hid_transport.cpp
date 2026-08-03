// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Two consumers key off this: the WinHidGateway claim filter (a Bluetooth pad
// must never become a USB-direct claim candidate) and the transport stamp the
// slot card renders. The paths below are real-world Windows shapes.

#include "core/input/HidTransport.h"

#include <catch2/catch_test_macros.hpp>

using dish::input::isBluetoothHidDevicePath;

TEST_CASE("HidTransport: a USB HID interface path is not Bluetooth", "[hidtransport]") {
    // A DualShock 4's wired interface path (SetupDi lowercased form).
    REQUIRE_FALSE(isBluetoothHidDevicePath(
        R"(\\?\hid#vid_054c&pid_05c4&mi_03#8&2f2a1b2&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})"));
}

TEST_CASE("HidTransport: the Bluetooth-classic HID service UUID reads as Bluetooth",
          "[hidtransport]") {
    // The same model over BT: the enumerator embeds the HID service class UUID
    // instead of the vid_/pid_ USB form. This shape leaking into enumerate() is
    // what grew a bogus "USB PATH" control on a wireless pad.
    REQUIRE(isBluetoothHidDevicePath(
        R"(\\?\hid#{00001124-0000-1000-8000-00805f9b34fb}_vid&0002054c_pid&05c4#9&1b2c3d4&0&0000#{4d1e55b2-f16f-11cf-88cb-001111000030})"));
}

TEST_CASE("HidTransport: BTHENUM / BTHLEDevice instance ids read as Bluetooth (any case)",
          "[hidtransport]") {
    // These are device instance ids, not interface paths; the helper accepts
    // either, and matches case-insensitively like Windows itself.
    REQUIRE(isBluetoothHidDevicePath(R"(BTHENUM\{00001124-0000-1000-8000-00805F9B34FB})"));
    REQUIRE(isBluetoothHidDevicePath(R"(\\?\hid#bthledevice&vid_045e&pid_0b13#7&abc)"));
}

TEST_CASE("HidTransport: empty / unknown paths fail safe to not-Bluetooth", "[hidtransport]") {
    // Failing to not-Bluetooth keeps a claimable USB pad from being filtered out.
    REQUIRE_FALSE(isBluetoothHidDevicePath(""));
    REQUIRE_FALSE(isBluetoothHidDevicePath("XInput#0"));
}
