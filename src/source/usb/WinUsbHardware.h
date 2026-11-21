// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A USB pad's raw hardware facts, read when asked: its interfaces from the device tree, and
// through its parent hub its device, configuration and HID report descriptors. How the port is
// found and what the requests carry is core/input/WinUsbTopology.h's.

#pragma once

#include "source/usb/UsbHardwareReading.h"

#include <string>

namespace dish::source::usb {

// `usbDeviceInstanceId` is UsbDeviceInfo::devicePath, the USB device node a HID collection belongs
// to. Several control transfers, any of which a sleeping pad can hold up, so never on the read,
// write or scan paths.
UsbHardwareReading readUsbHardware(const std::string& usbDeviceInstanceId);

} // namespace dish::source::usb
