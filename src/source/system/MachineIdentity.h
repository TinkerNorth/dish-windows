// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What the diagnostics bundle and the issue match know about this PC: its
// model (the firmware's SystemManufacturer and SystemProductName, under
// HKLM\HARDWARE\DESCRIPTION\System\BIOS), the Windows release and kernel
// version, and the CPU architecture. Read once; nothing here identifies the
// person or the machine beyond its model.

#pragma once

#include <QString>

namespace dish::source {

struct MachineIdentity {
    QString model;        // "AYN Thor", core/diagnostics/HostIdentity.h's machineModelOf
    QString os;           // "Windows 11 Version 23H2 (10.0.22631)"
    QString arch;         // "x86_64"
    int windowsMajor = 0; // 10 or 11
};

MachineIdentity readMachineIdentity();

} // namespace dish::source
