// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Read a Windows HID device-interface path: USB or Bluetooth, and which USB
// interface. A Bluetooth-connected pad must not become a USB-direct claim
// candidate: the BT report layout differs per model (a DS4 streams the short
// 0x01 report until a feature-report handshake), so a raw-HID claim over the BT
// interface decodes garbage. A HID interface path embeds the device instance id,
// so the enumerator is visible inline and matching markers suffices without a
// parent-devnode walk. An unmatched path reads as not-Bluetooth, which fails
// safe.

#pragma once

#include <algorithm>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

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

// The USB interface a HID device-interface path belongs to. A composite device's collections
// carry "&mi_XX" (two hex digits) in the instance id the path embeds; a single-interface device's
// carry none, and neither does a malformed one.
inline std::optional<int> hidInterfaceNumberFromPath(std::string_view path) {
    std::string lower(path);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    constexpr std::string_view kMarker = "&mi_";
    constexpr std::size_t kDigits = 2;
    const std::size_t at = lower.find(kMarker);
    if (at == std::string::npos || at + kMarker.size() + kDigits > lower.size()) {
        return std::nullopt;
    }
    const char* first = lower.data() + at + kMarker.size();
    int value = 0;
    const auto res = std::from_chars(first, first + kDigits, value, 16);
    if (res.ec != std::errc{} || res.ptr != first + kDigits) { return std::nullopt; }
    return value;
}

} // namespace dish::input
