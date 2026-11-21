// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/system/PowerStatus.h"

#include <windows.h>

namespace dish::source {

std::optional<diagnostics::PowerStatusBytes> readPowerStatus() {
    SYSTEM_POWER_STATUS status{};
    if (::GetSystemPowerStatus(&status) == FALSE) { return std::nullopt; }
    return diagnostics::PowerStatusBytes{status.ACLineStatus, status.BatteryFlag,
                                         status.BatteryLifePercent, status.SystemStatusFlag};
}

} // namespace dish::source
