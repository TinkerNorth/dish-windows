// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/UpdateApply.h"

#include "installer/InstallCoordinator.h"
#include "installer/InstallPlan.h"
#include "installer/Journal.h"
#include "installer/Manifest.h"
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
#include <QFileInfo>
#include <QSaveFile>
#include <QThread>

namespace dish::installer {

namespace {

QString dirOf(const QString& absFile) {
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(absFile));
    const int slash = clean.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? clean.left(slash) : clean;
}

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

// True when the record claims this location but no .lnk is there any more.
// The recorded absolute path wins over the freshly resolved one: a user whose
// known folder moved after the install still has their shortcut where the
// record says, and re-resolving would read that as "deleted".
bool shortcutWasDeleted(ShortcutLocation location, Scope scope, const InstalledManifest& installed,
                        ShortcutOps& shortcutOps) {
    const QString resolved = shortcutLinkPath(location, scope);
    QString recorded;
    for (const QString& path : installed.shortcutPaths) {
        if (QDir::cleanPath(QDir::fromNativeSeparators(path))
                .compare(QDir::cleanPath(QDir::fromNativeSeparators(resolved)),
                         Qt::CaseInsensitive) == 0) {
            recorded = path;
            break;
        }
    }
    const QString probe = recorded.isEmpty() ? resolved : recorded;
    if (probe.isEmpty()) { return false; } // unresolvable folder: leave the choice alone
    return !shortcutOps.exists(probe);
}

// Turns off any recorded shortcut choice whose link is gone from disk.
void dropDeletedShortcuts(InstallPlan& plan, const InstalledManifest& installed,
                          ShortcutOps& shortcutOps, Logger& logger) {
    if (plan.startMenu &&
        shortcutWasDeleted(ShortcutLocation::StartMenu, plan.scope, installed, shortcutOps)) {
        plan.startMenu = false;
        logger.line(
            QStringLiteral("apply: the recorded Start Menu shortcut is gone; not restoring"));
    }
    if (plan.desktop &&
        shortcutWasDeleted(ShortcutLocation::Desktop, plan.scope, installed, shortcutOps)) {
        plan.desktop = false;
        logger.line(QStringLiteral("apply: the recorded Desktop shortcut is gone; not restoring"));
    }
}

} // namespace

bool writeApplyResult(const QString& dir, int exitCode) {
    const ExitCode code = static_cast<ExitCode>(exitCode);
    const QByteArray line =
        QByteArray(resultToken(code)) + ' ' + QByteArray::number(exitCode) + '\n';
    QSaveFile save(dir + QStringLiteral("/apply-result.txt")); // tmp + rename (H7)
    if (!save.open(QIODevice::WriteOnly)) { return false; }
    if (save.write(line) != line.size()) { return false; }
    return save.commit();
}

bool relaunchTargetAfterBusy(const CliOptions& options, FileOps& fileOps, ProcessOps& processOps,
                             Logger& logger) {
    if (options.mode != CliOptions::Mode::UpdateApply || !options.relaunch) { return false; }
    const QString targetExe = QDir::cleanPath(QDir::fromNativeSeparators(options.targetExe));
    if (targetExe.isEmpty() || !fileOps.exists(targetExe)) { return false; }
    const QString installDir = dirOf(targetExe);
    const OpResult launched = processOps.launchDetached(
        targetExe, QStringList{QStringLiteral("--no-update-handoff")}, installDir, true);
    if (!launched.ok) {
        logger.line(QStringLiteral("apply: busy, and restarting \"%1\" failed").arg(targetExe));
        return false;
    }
    logger.line(
        QStringLiteral("apply: busy; restarted \"%1\" with --no-update-handoff").arg(targetExe));
    return true;
}

UpdateApply::UpdateApply(Logger& logger) : logger_(logger) {}

int UpdateApply::run(const CliOptions& options, QCoreApplication& app) {
    Win32FileOps fileOps;
    Win32RegistryOps registryOps;
    Win32ShortcutOps shortcutOps;
    Win32ProcessOps processOps;

    // apply-result.txt lands beside the INVOKED exe (the staged
    // dish-setup.exe the app spawned); direct UI-exe runs fall back to our
    // own dir (H7).
    const QString resultDir = options.sourceExe.isEmpty()
                                  ? QDir::fromNativeSeparators(app.applicationDirPath())
                                  : dirOf(options.sourceExe);
    const QString targetExe = QDir::cleanPath(QDir::fromNativeSeparators(options.targetExe));
    const QString installDir = dirOf(targetExe);

    bool pidExited = false;
    const auto conclude = [&](ExitCode code) -> int {
        const int exitCode = static_cast<int>(code);
        if (!writeApplyResult(resultDir, exitCode)) {
            logger_.line(
                QStringLiteral("apply: could not write apply-result.txt in \"%1\"").arg(resultDir));
        }
        logger_.line(QStringLiteral("apply: exit %1 (%2)")
                         .arg(exitCode)
                         .arg(QLatin1String(resultToken(code))));
        if (options.relaunch && pidExited) {
            // H6: the user pressed restart; under no outcome may they end
            // with no app. Success starts the NEW exe; failure restarts the
            // OLD one with exactly the loop-breaker flag.
            const bool ok = code == ExitCode::Ok;
            const QString exe = ok ? installDir + QStringLiteral("/dish.exe") : targetExe;
            const QStringList argv =
                ok ? QStringList() : QStringList{QStringLiteral("--no-update-handoff")};
            if (fileOps.exists(exe)) {
                const OpResult launched = processOps.launchDetached(exe, argv, installDir, true);
                if (!launched.ok) {
                    logger_.line(QStringLiteral("apply: relaunch failed \"%1\"").arg(exe));
                }
            }
        }
        return exitCode;
    };

    // H2: the invoking app must be gone before anything mutates.
    if (!processOps.waitForPid(options.waitPid, 60000)) {
        logger_.line(QStringLiteral("apply: pid %1 still alive after 60 s").arg(options.waitPid));
        return conclude(ExitCode::AppRunning); // no relaunch: the app never exited
    }
    pidExited = true;

    // H1: upgrade the SPECIFIC install holding --target-exe; never a fresh
    // default-path install.
    const auto installed = loadInstalledManifest(installDir);
    if (!installed) {
        logger_.line(QStringLiteral("apply: no managed install at \"%1\"").arg(installDir));
        return conclude(ExitCode::NothingInstalled);
    }

    if (options.stagingDir.isEmpty()) {
        logger_.line(QStringLiteral("apply: no --staging (run the staged dish-setup.exe)"));
        return conclude(ExitCode::Usage);
    }
    const auto payload = loadPayloadManifest(options.stagingDir);
    if (!payload) {
        logger_.line(QStringLiteral("apply: staged manifest.json missing or invalid"));
        return conclude(ExitCode::PayloadCorrupt);
    }

    // H3: the swapped-staged-file TOCTOU gate, then the second downgrade line.
    if (payload->version != options.expectVersion) {
        logger_.line(QStringLiteral("apply: payload is %1 but --expect-version says %2")
                         .arg(payload->version, options.expectVersion));
        return conclude(ExitCode::VersionMismatch);
    }
    const auto cmp = compareVersions(payload->version, installed->version);
    if (!cmp || *cmp <= 0) {
        logger_.line(QStringLiteral("apply: payload %1 <= installed %2, refused")
                         .arg(payload->version, installed->version));
        return conclude(ExitCode::Downgrade);
    }

    // H5: scope comes from the record; machine scope self-elevates exactly
    // once, and the child re-runs the stub pipeline (re-verifying the
    // trailer itself).
    const auto scope = scopeFromToken(installed->scope);
    if (scope == Scope::AllUsers && !processOps.isElevated() && !options.elevated) {
        QString exe = options.sourceExe;
        if (exe.isEmpty()) { exe = QDir::fromNativeSeparators(app.applicationFilePath()); }
        CliOptions childOptions = options;
        childOptions.elevated = true;
        childOptions.stagingDir.clear(); // the child's stub extracts fresh
        childOptions.sourceExe.clear();
        bool declined = false;
        logger_.line(QStringLiteral("apply: machine scope, elevating \"%1\"").arg(exe));
        const auto childExit = processOps.runElevatedWait(exe, childOptions.toArgv(), &declined);
        if (!childExit) {
            logger_.line(declined ? QStringLiteral("apply: UAC declined")
                                  : QStringLiteral("apply: elevated relaunch failed"));
            return conclude(declined ? ExitCode::Elevation : ExitCode::Internal);
        }
        // The child owned the duties (result file, relaunch); mirror only.
        logger_.line(QStringLiteral("apply: elevated child exited %1").arg(*childExit));
        return *childExit;
    }

    // H2 second half: OTHER processes still running from the install dir
    // (second instance, another user) fail the apply; NEVER terminated.
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (processOps.processesUnder(installDir).isEmpty()) { break; }
        QThread::msleep(500);
    }
    if (!processOps.processesUnder(installDir).isEmpty()) {
        logger_.line(
            QStringLiteral("apply: other Dish processes still run from \"%1\"").arg(installDir));
        return conclude(ExitCode::AppRunning);
    }

    // Recorded choices verbatim (H1); Abort policy, never terminate (H2).
    InstallPlan plan;
    plan.scope = scope.value_or(Scope::PerUser);
    plan.installDir = installDir;
    plan.startMenu = installed->startMenu;
    plan.desktop = installed->desktop;
    // ...with one subtraction the record cannot express: a shortcut the user
    // deleted by hand stays deleted (spec 11.2 "never re-create a deleted
    // shortcut", asserted by spec 12.2 step 6). H1's "verbatim" is about the
    // installer never INVENTING a choice; silently putting a desktop icon back
    // during an unattended update is the behaviour the Shortcuts page's
    // no-junk promise rules out. The corrected set is what gets recorded, so
    // the next upgrade agrees and the uninstaller does not hunt for a ghost.
    dropDeletedShortcuts(plan, *installed, shortcutOps, logger_);
    plan.launch = false; // relaunch duties are H6's, not the pipeline's
    plan.allowDowngrade = false;
    plan.closePolicy = ClosePolicy::Abort;
    plan.isUpgrade = true;
    plan.existingVersion = installed->version;
    plan.existingDir = installDir;

    if (fileOps.exists(journalFilePath(installDir))) {
        logger_.line(QStringLiteral("apply: recovering stale journal first"));
        recoverStaleJournal(installDir, fileOps, registryOps, shortcutOps);
    }

    InstallCoordinator coordinator(fileOps, registryOps, shortcutOps, processOps, logger_);
    coordinator.setSilent(true);
    coordinator.setStagingDir(options.stagingDir);
    coordinator.setOldManifest(installed);

    int exitCode = static_cast<int>(ExitCode::Internal);
    QEventLoop loop;
    QObject::connect(&coordinator, &InstallCoordinator::finished, &loop,
                     [&exitCode, &loop](ExitCode code, SetupError, const QString&) {
                         exitCode = static_cast<int>(code);
                         loop.quit();
                     });
    coordinator.start(plan, *payload);
    loop.exec();

    // H8: no staging cleanup on success — the relaunched app's janitor owns
    // %LOCALAPPDATA%\Dish\updates.
    return conclude(static_cast<ExitCode>(exitCode));
}

} // namespace dish::installer
