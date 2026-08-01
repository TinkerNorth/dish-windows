// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// HidTransport — the PURE, Qt/Win32-free classification of a Windows HID
// device-interface path into its transport (USB vs Bluetooth). One definition
// of the marker set, shared by the two places that see a device path:
//
//   * WinHidGateway::enumerate() — a Bluetooth-connected pad must NOT become a
//     USB-direct claim candidate. The raw-HID claim is a USB feature: the BT
//     report layout differs per model (a DS4 streams the short 0x01 report
//     until a feature-report handshake), so a claim over the BT interface
//     decodes garbage — and the pad would grow a bogus "USB PATH" control.
//   * SDLGamepadBridge — SDL_JoystickPath() returns the same Win32 HID
//     interface path for HIDAPI/RawInput devices; the bridge stamps
//     Device::bluetooth from it so the UI can show Bluetooth iconography.
//
// ── Why string markers are sufficient ────────────────────────────────────────
// A Windows HID interface path embeds the device instance id. For a USB pad it
// reads `\\?\hid#vid_054c&pid_05c4&mi_03#...`; for a Bluetooth-classic HID
// device the enumerator is BTHENUM and the path carries the HID service class
// UUID: `\\?\hid#{00001124-0000-1000-8000-00805f9b34fb}_vid&0002054c_pid&05c4#…`;
// a BLE HID device enumerates under BTHLEDevice. Matching those three markers
// is the same heuristic hidapi's hid_internal_detect_bus_type applies without
// the parent-devnode walk (our inputs are always HID *interface* paths, where
// the enumerator is visible inline). An unmatched path reads as not-Bluetooth,
// which fails safe: the pad keeps today's USB presentation.

#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace dish::input {

// True iff a Windows HID device-interface path (or device instance id) belongs
// to a Bluetooth-connected device — classic (BTHENUM / the HID service UUID
// {00001124-…}) or BLE (BTHLEDevice). Case-insensitive; empty/unknown -> false.
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
