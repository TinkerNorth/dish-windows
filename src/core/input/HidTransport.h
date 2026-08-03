// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Classify a Windows HID device-interface path as USB or Bluetooth. A
// Bluetooth-connected pad must not become a USB-direct claim candidate: the BT
// report layout differs per model (a DS4 streams the short 0x01 report until a
// feature-report handshake), so a raw-HID claim over the BT interface decodes
// garbage. A HID interface path embeds the device instance id, so the enumerator
// is visible inline and matching markers suffices without a parent-devnode walk.
// An unmatched path reads as not-Bluetooth, which fails safe.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace dish::input {

// Classic BT enumerates under BTHENUM and carries the HID service class UUID
// {00001124-...}; BLE enumerates under BTHLEDevice.
inline bool isBluetoothHidDevicePath(std::string_view path) {
    if (path.empty()) { return false; }
    std::string lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return lower.find("bthenum") != std::string::npos ||
           lower.find("bthledevice") != std::string::npos ||
           lower.find("{00001124-0000-1000-8000-00805f9b34fb}") != std::string::npos;
}

} // namespace dish::input
