// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A USB pad's hardware as the platform gives it, raw, for the bundle's `hardware` and `capture`
// (docs/diagnostics-bundle.md): what the bytes say is the shared descriptor parsers' to decide.
// Plain values, so the claim machinery and its fakes carry them with no platform type in sight.

#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace dish::source::usb {

// One interface the device tree lists: its number and the class its compatible id names.
struct UsbInterfaceEntry {
    int number = 0;
    std::uint8_t classCode = 0;
    std::uint8_t subclass = 0;
    std::uint8_t protocol = 0;

    bool operator==(const UsbInterfaceEntry&) const = default;
};

// One descriptor asked of the pad through its hub: the bytes it answered with, or the Win32 error
// the request ended with. Neither when it was never asked.
struct HubDescriptor {
    std::vector<std::uint8_t> bytes;
    std::optional<std::uint32_t> refusal;
};

struct UsbReportDescriptor {
    int interfaceNumber = 0;
    HubDescriptor descriptor;
};

struct UsbHardwareReading {
    std::vector<UsbInterfaceEntry> interfaces;
    // Why the hub could not be asked at all: no device node, no hub or port for it, or an open the
    // hub refused. The descriptors below are then never asked.
    std::optional<std::uint32_t> hubUnreachable;
    HubDescriptor device;
    HubDescriptor configuration;
    // One per HID interface, in the order of `interfaces`. A refused one keeps no bytes, and none
    // is ever rebuilt from the HID class driver's preparsed data.
    std::vector<UsbReportDescriptor> reportDescriptors;
};

} // namespace dish::source::usb
