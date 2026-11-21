// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// How many USB host controllers Windows reports present: the device interfaces of class
// GUID_DEVINTERFACE_USB_HOST_CONTROLLER that SetupAPI lists with DIGCF_PRESENT, the way
// Microsoft's USBView sample finds them. Read when a diagnostics file is prepared; what the count
// means is core/diagnostics/UsbHost.h's to decide.

#pragma once

#include <cstddef>
#include <optional>

namespace dish::source {

// Empty when SetupAPI gave no answer.
std::optional<std::size_t> countUsbHostControllers();

} // namespace dish::source
