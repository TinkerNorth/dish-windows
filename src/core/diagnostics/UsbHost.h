// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// `device.usbHost` (docs/diagnostics-bundle.md, "Device facts"): measured, never assumed. The
// system layer counts what the platform reports, the present device interfaces of class
// GUID_DEVINTERFACE_USB_HOST_CONTROLLER on Windows and the root hubs under /sys/bus/usb/devices on
// Linux, and passes the count in, or nothing when the platform gave no answer.

#pragma once

#include <cstddef>
#include <optional>

namespace dish::diagnostics {

inline std::optional<bool> usbHostFromCount(const std::optional<std::size_t>& hostControllers) {
    if (!hostControllers.has_value()) { return std::nullopt; }
    return *hostControllers > 0;
}

} // namespace dish::diagnostics
