// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/SilentRunner.h"

#include "installer/InstallCoordinator.h"
#include "installer/InstallPlan.h"
#include "installer/Journal.h"
#include "installer/Manifest.h"
#include "installer/UninstallCoordinator.h"
#include "installer/VersionCompare.h"
#include "installer/ops/KnownFolders.h"
#include "installer/ops/Win32FileOps.h"
#include "installer/ops/Win32ProcessOps.h"
#include "installer/ops/Win32RegistryOps.h"
#include "installer/ops/Win32ShortcutOps.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>

namespace dish::installer {

namespace {

std::optional<PayloadManifest> loadPayloadManifest(const QString& stagingDir) {
    QFile file(stagingDir + QStringLiteral("/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
    return PayloadManifest::fromJson(file.readAll());
}

std::optional<InstalledManifest> loadInstalledManifest(const QString& installDir) {
    QFile file(installDir + QLatin1Char('/') + installedManifestFileName());
    if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
    return InstalledManifest::fromJson(file.readAll());
}

} // namespace

SilentRunner::SilentRunner(Logger& logger) : logger_(logger) {}

int SilentRunner::run(const CliOptions& options, QCoreApplication& app) {
    switch (options.mode) {
    case CliOptions::Mode::SilentInstall:
        return runInstall(options, app);
    case CliOptions::Mode::SilentUninstall:
        return runUninstall(options, app);
    case CliOptions::Mode::ExtractOnly:
        return runExtractOnly(options);
    default:
        break;
    }
    logger_.line(QStringLiteral("silent: mode not runnable headless"));
    return static_cast<int>(ExitCode::Usage);
}

int SilentRunner::runInstall(const CliOptions& options, QCoreApplication& app) {
    Q_UNUSED(app);
    Win32FileOps fileOps;
    Win32RegistryOps registryOps;
    Win32ShortcutOps shortcutOps;
    Win32ProcessOps processOps;

    if (options.stagingDir.isEmpty()) {
        logger_.line(QStringLiteral("silent: no --staging (run dish-setup.exe, not the UI exe)"));
        return static_cast<int>(ExitCode::Usage);
    }
    const auto manifest = loadPayloadManifest(options.stagingDir);
    if (!manifest) {
        logger_.line(QStringLiteral("silent: staged manifest.json missing or invalid"));
        return static_cast<int>(ExitCode::PayloadCorrupt);
    }

    InstallPlan plan = options.plan;

    // Existing-install probe: silent picks the hive by --scope (spec 11.2).
    std::optional<InstalledManifest> oldManifest;
    if (const auto arp = registryOps.readInstalled(plan.scope)) {
        const QString location = QDir::fromNativeSeparators(arp->installLocation);
        if (auto recorded = loadInstalledManifest(location)) {
            oldManifest = recorded;
            plan.isUpgrade = true;
            plan.installDir = location; // dir + scope lock to the existing install
            plan.existingVersion = recorded->version;
            plan.existingDir = location;
        }
    }

    if (plan.isUpgrade) {
        const auto cmp = compareVersions(manifest->version, plan.existingVersion);
        if (cmp && *cmp < 0 && !plan.allowDowngrade) {
            logger_.line(QStringLiteral("silent: downgrade %1 -> %2 refused")
                             .arg(plan.existingVersion, manifest->version));
            return static_cast<int>(ExitCode::Downgrade);
        }
    }

    if (plan.scope == Scope::AllUsers && !processOps.isElevated()) {
        logger_.line(QStringLiteral("silent: machine scope needs an elevated caller"));
        return static_cast<int>(ExitCode::Elevation); // plain silent never elevates (D6)
    }

    if (plan.installDir.isEmpty()) { plan.installDir = defaultInstallDir(plan.scope); }
    const DirStatus dirStatus = validateInstallDir(plan.installDir);
    if (dirStatus != DirStatus::Ok && dirStatus != DirStatus::IsExistingInstall &&
        !(dirStatus == DirStatus::NotEmpty && plan.isUpgrade)) {
        logger_.line(QStringLiteral("silent: install dir rejected (\"%1\")").arg(plan.installDir));
        return static_cast<int>(ExitCode::Usage);
    }

    // A crashed previous attempt is swept automatically in silent mode
    // (spec 11.2).
    if (fileOps.exists(journalFilePath(plan.installDir))) {
        logger_.line(QStringLiteral("silent: recovering stale journal first"));
        recoverStaleJournal(plan.installDir, fileOps, registryOps, shortcutOps);
    }

    InstallCoordinator coordinator(fileOps, registryOps, shortcutOps, processOps, logger_);
    coordinator.setSilent(true);
    coordinator.setStagingDir(options.stagingDir);
    coordinator.setOldManifest(oldManifest);

    int exitCode = static_cast<int>(ExitCode::Internal);
    QEventLoop loop;
    QObject::connect(&coordinator, &InstallCoordinator::finished, &loop,
                     [&exitCode, &loop](ExitCode code, SetupError, const QString&) {
                         exitCode = static_cast<int>(code);
                         loop.quit();
                     });
    coordinator.start(plan, *manifest);
    loop.exec();
    return exitCode;
}

int SilentRunner::runUninstall(const CliOptions& options, QCoreApplication& app) {
    Win32FileOps fileOps;
    Win32RegistryOps registryOps;
    Win32ShortcutOps shortcutOps;
    Win32ProcessOps processOps;

    const QString ownDir = QDir::fromNativeSeparators(app.applicationDirPath());
    const auto manifest = loadInstalledManifest(ownDir);
    if (!manifest) {
        logger_.line(QStringLiteral("silent: no .dish-manifest.json beside this exe"));
        return static_cast<int>(ExitCode::NothingInstalled);
    }

    const auto scope = scopeFromToken(manifest->scope);
    if (scope == Scope::AllUsers && !processOps.isElevated()) {
        logger_.line(QStringLiteral("silent: machine-scope uninstall needs an elevated caller"));
        return static_cast<int>(ExitCode::Elevation);
    }

    // Leftovers of a crashed upgrade restore first so the manifest-driven
    // removal sees a consistent tree.
    if (fileOps.exists(journalFilePath(ownDir))) {
        recoverStaleJournal(ownDir, fileOps, registryOps, shortcutOps);
    }

    UninstallCoordinator coordinator(fileOps, registryOps, shortcutOps, processOps, logger_);
    coordinator.setSilent(true);
    coordinator.setClosePolicy(options.plan.closePolicy);
    coordinator.setPurgeUserData(options.purgeUserData);
    coordinator.setWorkingSetProbe(
        [&processOps](const QString& dir) { return processOps.ownWorkingSetUnder(dir); });
    coordinator.setPurgeRegistryHook([] { Win32RegistryOps::purgeUserSettingsTrees(); });

    int exitCode = static_cast<int>(ExitCode::Internal);
    QEventLoop loop;
    QObject::connect(&coordinator, &UninstallCoordinator::finished, &loop,
                     [&exitCode, &loop](ExitCode code, SetupError, const QString&) {
                         exitCode = static_cast<int>(code);
                         loop.quit();
                     });
    InstalledManifest toRemove = *manifest;
    toRemove.installDir = ownDir; // trust where we actually run from
    coordinator.start(toRemove);
    loop.exec();
    return exitCode;
}

int SilentRunner::runExtractOnly(const CliOptions& options) {
    Win32FileOps fileOps;
    if (options.stagingDir.isEmpty()) {
        logger_.line(QStringLiteral("extract: no --staging (run dish-setup.exe, not the UI exe)"));
        return static_cast<int>(ExitCode::Usage);
    }
    const auto manifest = loadPayloadManifest(options.stagingDir);
    if (!manifest) {
        logger_.line(QStringLiteral("extract: staged manifest.json missing or invalid"));
        return static_cast<int>(ExitCode::PayloadCorrupt);
    }
    const QString dest = options.extractDir;
    if (!fileOps.ensureDir(dest).ok) {
        logger_.line(QStringLiteral("extract: cannot create \"%1\"").arg(dest));
        return static_cast<int>(ExitCode::RolledBack);
    }
    for (const PayloadEntry& entry : manifest->files) {
        const QString from = options.stagingDir + QLatin1Char('/') + entry.stagedAs;
        const QString to = dest + QLatin1Char('/') + entry.path;
        const int slash = to.lastIndexOf(QLatin1Char('/'));
        if (slash > 0 && !fileOps.ensureDir(to.left(slash)).ok) {
            return static_cast<int>(ExitCode::RolledBack);
        }
        if (!fileOps.copyWithProgress(from, to, nullptr).ok) {
            logger_.line(QStringLiteral("extract: copy failed \"%1\"").arg(entry.path));
            return static_cast<int>(ExitCode::RolledBack);
        }
        if (!fileOps.verifySha256(to, entry.sha256Hex).ok) {
            logger_.line(QStringLiteral("extract: hash mismatch \"%1\"").arg(entry.path));
            return static_cast<int>(ExitCode::PayloadCorrupt);
        }
    }
    logger_.line(
        QStringLiteral("extract: %1 files into \"%2\"").arg(manifest->files.size()).arg(dest));
    return static_cast<int>(ExitCode::Ok);
}

} // namespace dish::installer
