// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/UsbHostProbe.h"

// <windows.h> first, then SetupAPI; initguid.h before usbiodef.h so the interface class GUID is
// defined in this unit, as Microsoft's page for GUID_DEVINTERFACE_USB_HOST_CONTROLLER requires.
#include <windows.h>

#include <setupapi.h>

#include <initguid.h>

#include <usbiodef.h>

namespace dish::source {

std::optional<int> countUsbHostControllers() {
    const HDEVINFO set = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USB_HOST_CONTROLLER, nullptr,
                                              nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (set == INVALID_HANDLE_VALUE) { return std::nullopt; }
    int count = 0;
    SP_DEVICE_INTERFACE_DATA entry{};
    entry.cbSize = sizeof(entry);
    while (SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                                       static_cast<DWORD>(count), &entry) != 0) {
        ++count;
    }
    // The walk ends on ERROR_NO_MORE_ITEMS; anything else means the count is not the answer.
    const bool walkedToTheEnd = GetLastError() == ERROR_NO_MORE_ITEMS;
    SetupDiDestroyDeviceInfoList(set);
    if (!walkedToTheEnd) { return std::nullopt; }
    return count;
}

} // namespace dish::source
