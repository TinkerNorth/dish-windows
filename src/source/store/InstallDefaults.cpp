// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "source/store/InstallDefaults.h"

#include <QCoreApplication>
#include <QLoggingCategory>
#include <QSettings>
#include <QVariant>

namespace dish::source {

namespace {

Q_LOGGING_CATEGORY(lcInstall, "dish.install")

std::optional<bool> readChoice(const QSettings& scope, const char* key) {
    const QLatin1String name(key);
    if (!scope.contains(name)) { return std::nullopt; }
    return scope.value(name).toBool();
}

std::optional<InstallDefaults> readScope(const QSettings& scope) {
    const QString stamp = scope.value(QLatin1String(kKeyInstallDefaultsStamp)).toString();
    if (stamp.isEmpty()) { return std::nullopt; }
    InstallDefaults defaults;
    defaults.stamp = stamp;
    defaults.updateCheck = readChoice(scope, kKeyInstallDefaultsUpdateCheck);
    defaults.crashReports = readChoice(scope, kKeyInstallDefaultsCrashReports);
    return defaults;
}

QString describe(const std::optional<bool>& choice) {
    if (!choice.has_value()) { return QStringLiteral("kept"); }
    return *choice ? QStringLiteral("on") : QStringLiteral("off");
}

} // namespace

std::optional<InstallDefaults> readInstallDefaults(const QSettings& userScope,
                                                   const QSettings& systemScope) {
    auto fromUser = readScope(userScope);
    if (fromUser.has_value()) { return fromUser; }
    return readScope(systemScope);
}

std::optional<InstallDefaults> applyInstallDefaultsOnce(const QSettings& userScope,
                                                        const QSettings& systemScope,
                                                        QSettings& appSettings,
                                                        UpdatePreferenceStore& updates,
                                                        CrashReportingStore& crashReports) {
    auto defaults = readInstallDefaults(userScope, systemScope);
    if (!defaults.has_value()) { return std::nullopt; }

    const QString applied = appSettings.value(QLatin1String(kKeyInstallDefaultsApplied)).toString();
    if (defaults->stamp == applied) { return std::nullopt; }

    if (defaults->updateCheck.has_value()) { updates.setChecksEnabled(*defaults->updateCheck); }
    if (defaults->crashReports.has_value()) { crashReports.setEnabled(*defaults->crashReports); }
    appSettings.setValue(QLatin1String(kKeyInstallDefaultsApplied), defaults->stamp);
    appSettings.sync();

    qCInfo(lcInstall).noquote()
        << QStringLiteral(
               "installer defaults applied: update check %1, crash reports %2 (stamp %3)")
               .arg(describe(defaults->updateCheck), describe(defaults->crashReports),
                    defaults->stamp);
    return defaults;
}

std::optional<InstallDefaults> applyInstallDefaultsFromRegistry() {
    const QString organization = QCoreApplication::organizationName();
    const QString application = QCoreApplication::applicationName();
    QSettings userScope(QSettings::NativeFormat, QSettings::UserScope, organization, application);
    QSettings systemScope(QSettings::NativeFormat, QSettings::SystemScope, organization,
                          application);
    userScope.setFallbacksEnabled(false);
    systemScope.setFallbacksEnabled(false);
    QSettings appSettings;
    UpdatePreferenceStore updates;
    CrashReportingStore crashReports;
    return applyInstallDefaultsOnce(userScope, systemScope, appSettings, updates, crashReports);
}

} // namespace dish::source
