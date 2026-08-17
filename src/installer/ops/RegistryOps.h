// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The abstract registry seam for the ARP (Add/Remove Programs) key, the
// installer's ONLY registry footprint (spec D5 and section 10).

#pragma once

#include "installer/InstallPlan.h"
#include "installer/ops/FileOps.h"

#include <QString>

#include <optional>

namespace dish::installer {

// Values for Uninstall\TinkerNorth.Dish, section 10 order. An empty
// installDate means "stamp today's yyyyMMdd at write time": reducers build
// this struct and reducers cannot read the clock.
struct ArpValues {
    QString displayName;
    QString displayVersion;
    int versionMajor = 0;
    int versionMinor = 0;
    QString publisher;
    QString displayIcon;
    QString installLocation;
    QString installDate;
    QString uninstallString;
    QString quietUninstallString;
    quint32 estimatedSizeKiB = 0;
    QString urlInfoAbout;
    QString helpLink;
    QString installScope; // "user" | "machine"

    bool operator==(const ArpValues& o) const {
        return displayName == o.displayName && displayVersion == o.displayVersion &&
               versionMajor == o.versionMajor && versionMinor == o.versionMinor &&
               publisher == o.publisher && displayIcon == o.displayIcon &&
               installLocation == o.installLocation && installDate == o.installDate &&
               uninstallString == o.uninstallString &&
               quietUninstallString == o.quietUninstallString &&
               estimatedSizeKiB == o.estimatedSizeKiB && urlInfoAbout == o.urlInfoAbout &&
               helpLink == o.helpLink && installScope == o.installScope;
    }
    bool operator!=(const ArpValues& o) const { return !(*this == o); }
};

// What the upgrade probe needs from an existing ARP entry. The authoritative
// record is .dish-manifest.json at installLocation; these values cross-check it.
struct InstalledInfo {
    QString displayVersion;
    QString installLocation;
    QString installScope; // "user" | "machine" (our own hint value)

    bool operator==(const InstalledInfo& o) const {
        return displayVersion == o.displayVersion && installLocation == o.installLocation &&
               installScope == o.installScope;
    }
    bool operator!=(const InstalledInfo& o) const { return !(*this == o); }
};

class RegistryOps {
  public:
    virtual ~RegistryOps() = default;
    virtual OpResult writeArp(Scope scope, const ArpValues& values) = 0;
    virtual OpResult deleteArp(Scope scope) = 0;
    // nullopt when the hive has no TinkerNorth.Dish key; the caller probes both
    // hives itself.
    virtual std::optional<InstalledInfo> readInstalled(Scope scope) = 0;
};

} // namespace dish::installer
