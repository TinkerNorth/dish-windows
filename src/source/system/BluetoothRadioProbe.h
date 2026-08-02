// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// BluetoothRadioProbe — "is there a Bluetooth radio, and is it switched on?".
//
// The wizard's waiting step is the surface a first-run user stares at when
// nothing shows up, and "the radio is off" is one of the two commonest reasons.
// The two facts are genuinely different and need different copy:
//
//   present && !enabled  -> "Bluetooth is off on this PC." + Open settings
//   !present             -> "This PC has no Bluetooth adapter." (no button)
//
// so they are probed separately: SetupAPI answers "a Bluetooth-class device
// exists" (true even with the radio switched off, which BluetoothFindFirstRadio
// is not), and BluetoothFindFirstRadio answers "a radio handle opens".
//
// Header-only + inline so the single consumer (AppViewModel) needs no library
// entry; the Win32 half is compiled only under Q_OS_WIN and every other
// platform gets {false, false} — which renders the "no adapter" copy, the safe
// default. No unit test: this is pure OS I/O with no decision in it.

#pragma once

#include <QtGlobal>

#ifdef Q_OS_WIN
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// Order matters: these three all require windows.h first.
#include <bluetoothapis.h>
#include <devguid.h>
#include <setupapi.h>
#if defined(_MSC_VER)
// BluetoothFindFirstRadio lives in bthprops; GUID_DEVCLASS_BLUETOOTH is a
// devguid symbol out of uuid. Declared here rather than in the build file so
// the probe is self-contained wherever it is included.
#pragma comment(lib, "bthprops.lib")
#pragma comment(lib, "uuid.lib")
#endif
#endif

namespace dish::source {

struct BluetoothRadioState {
    bool present = false;
    bool enabled = false;
};

// present : a Bluetooth-class device exists (SetupAPI, GUID_DEVCLASS_BLUETOOTH)
//           -- true even when the radio is switched off.
// enabled : BluetoothFindFirstRadio() returned a handle.
inline BluetoothRadioState probeBluetoothRadio() {
    BluetoothRadioState state;
#ifdef Q_OS_WIN
    HDEVINFO devInfo =
        SetupDiGetClassDevsW(&GUID_DEVCLASS_BLUETOOTH, nullptr, nullptr, DIGCF_PRESENT);
    if (devInfo != INVALID_HANDLE_VALUE) {
        SP_DEVINFO_DATA data{};
        data.cbSize = sizeof(SP_DEVINFO_DATA);
        state.present = SetupDiEnumDeviceInfo(devInfo, 0, &data) != FALSE;
        SetupDiDestroyDeviceInfoList(devInfo);
    }

    BLUETOOTH_FIND_RADIO_PARAMS params{};
    params.dwSize = sizeof(BLUETOOTH_FIND_RADIO_PARAMS);
    HANDLE radio = nullptr;
    const HBLUETOOTH_RADIO_FIND find = BluetoothFindFirstRadio(&params, &radio);
    if (find != nullptr) {
        state.enabled = true;
        // A radio that opens is also a radio that exists — SetupAPI can miss a
        // stack that presents no device node.
        state.present = true;
        if (radio != nullptr) { CloseHandle(radio); }
        BluetoothFindRadioClose(find);
    }
#endif
    return state;
}

} // namespace dish::source
