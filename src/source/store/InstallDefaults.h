// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "source/store/CrashReportingStore.h"
#include "source/store/UpdatePreferenceStore.h"

#include <QString>

#include <optional>

class QSettings;

namespace dish::source {

inline constexpr const char* kKeyInstallDefaultsStamp = "InstallDefaults/Stamp";
inline constexpr const char* kKeyInstallDefaultsUpdateCheck = "InstallDefaults/UpdateCheck";
inline constexpr const char* kKeyInstallDefaultsCrashReports = "InstallDefaults/CrashReports";
inline constexpr const char* kKeyInstallDefaultsApplied = "install_defaults_stamp";

struct InstallDefaults {
    QString stamp;
    std::optional<bool> updateCheck;
    std::optional<bool> crashReports;

    bool operator==(const InstallDefaults& o) const {
        return stamp == o.stamp && updateCheck == o.updateCheck && crashReports == o.crashReports;
    }
    bool operator!=(const InstallDefaults& o) const { return !(*this == o); }
};

std::optional<InstallDefaults> readInstallDefaults(const QSettings& userScope,
                                                   const QSettings& systemScope);

std::optional<InstallDefaults> applyInstallDefaultsOnce(const QSettings& userScope,
                                                        const QSettings& systemScope,
                                                        QSettings& appSettings,
                                                        UpdatePreferenceStore& updates,
                                                        CrashReportingStore& crashReports);

std::optional<InstallDefaults> applyInstallDefaultsFromRegistry();

} // namespace dish::source
