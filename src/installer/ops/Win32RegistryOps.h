// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The production RegistryOps: the ARP (Add/Remove Programs) key of spec
// section 10, written to the 64-bit view (KEY_WOW64_64KEY) under HKCU or HKLM
// per scope. Plus the uninstaller's purge helper for the two HKCU settings
// trees (concrete-only: purge is not part of the reducer-facing seam).

#pragma once

#include "installer/ops/RegistryOps.h"

namespace dish::installer {

class Win32RegistryOps : public RegistryOps {
  public:
    Win32RegistryOps() = default;
    ~Win32RegistryOps() override = default;

    // Creates the key when missing and writes every section 10 value. An empty
    // values.installDate is stamped with today's yyyyMMdd here (the reducers
    // that build ArpValues cannot read the clock).
    OpResult writeArp(Scope scope, const ArpValues& values) override;
    OpResult deleteArp(Scope scope) override;
    std::optional<InstalledInfo> readInstalled(Scope scope) override;

    // --purge-user-data: RegDeleteTree of HKCU\Software\Dish\Dish and
    // HKCU\Software\TinkerNorth\Dish for the INVOKING user (a machine-scope
    // purge does not attempt other users' hives; documented honestly).
    // Best-effort; missing keys are success.
    static void purgeUserSettingsTrees();
};

} // namespace dish::installer
