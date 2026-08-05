// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "update/UpdateHandoff.h"

#include "core/reducer/UpdateMachine.h"
#include "core/update/UpdateVersion.h"
#include "source/store/UpdatePreferenceStore.h"
#include "update/FileStagingStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>

#include <string>
#include <vector>

// Pulls windows.h, so it stays last: no Win32 macro may reach the Qt headers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::update {

namespace {

bool hasNoHandoffFlag(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == nullptr) { continue; }
        if (qstricmp(argv[i], UpdateHandoff::kNoHandoffFlag) == 0) { return true; }
    }
    return false;
}

// One argument, quoted only when it needs to be. The installer's own CLI
// accepts either form; quoting paths unconditionally keeps spaces safe.
void appendQuoted(QString& commandLine, const QString& value) {
    commandLine += QLatin1Char(' ');
    commandLine += QLatin1Char('"');
    commandLine += QDir::toNativeSeparators(value);
    commandLine += QLatin1Char('"');
}

} // namespace

RunningInstanceMutex::RunningInstanceMutex() {
    // Not an error when it already exists: this is a presence beacon, not a
    // single-instance guard, and the second instance must still run.
    handle_ = ::CreateMutexW(nullptr, FALSE, UpdateHandoff::kRunningMutexName);
}

RunningInstanceMutex::~RunningInstanceMutex() {
    if (handle_ != nullptr) { ::CloseHandle(static_cast<HANDLE>(handle_)); }
}

bool UpdateHandoff::anotherInstanceRunning() {
    HANDLE existing = ::OpenMutexW(SYNCHRONIZE, FALSE, kRunningMutexName);
    if (existing == nullptr) { return false; }
    ::CloseHandle(existing);
    return true;
}

QString UpdateHandoff::runningExecutablePath() {
    std::vector<wchar_t> buffer(MAX_PATH);
    for (;;) {
        const DWORD written =
            ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0) { return {}; }
        if (written < buffer.size() - 1) {
            return QDir::fromNativeSeparators(
                QString::fromWCharArray(buffer.data(), static_cast<int>(written)));
        }
        if (buffer.size() >= 32768) { return {}; }
        buffer.resize(buffer.size() * 2);
    }
}

std::optional<StagedUpdate> UpdateHandoff::verifiedStageForApply(const QString& currentVersion) {
    FileStagingStore store;
    if (store.root().isEmpty()) { return std::nullopt; }

    auto staged = store.findStaged();
    if (!staged.has_value()) { return std::nullopt; }

    // Re-evaluated against the CURRENT exe version, not against whatever was
    // true when the stage was made: a manual upgrade in the meantime must
    // neutralize the stage rather than downgrade the machine.
    if (!isStrictlyNewer(staged->version, currentVersion)) {
        store.discard(staged->version);
        return std::nullopt;
    }

    QSettings settings{QLatin1String(kSettingsOrganization), QLatin1String(kSettingsApplication)};
    const QString recorded =
        settings.value(QLatin1String(source::kKeyUpdatesHandoffVersion)).toString();
    const int attempts =
        recorded == staged->version
            ? settings.value(QLatin1String(source::kKeyUpdatesHandoffAttempts), 0).toInt()
            : 0;
    if (attempts >= reducer::kMaxApplyAttemptsPerVersion) {
        quarantine(*staged);
        return std::nullopt;
    }

    // The expensive guard, deliberately last: ~100-300 ms of hashing, and only
    // on the launches that are actually about to hand off. It is what catches
    // corruption (or tampering) between the promote and this boot.
    const QString digest = sha256OfFile(staged->exePath);
    if (digest.isEmpty() || digest.compare(staged->sha256, Qt::CaseInsensitive) != 0) {
        store.discard(staged->version);
        return std::nullopt;
    }
    return staged;
}

bool UpdateHandoff::recordAttempt(const StagedUpdate& staged) {
    QSettings settings{QLatin1String(kSettingsOrganization), QLatin1String(kSettingsApplication)};
    const QString recorded =
        settings.value(QLatin1String(source::kKeyUpdatesHandoffVersion)).toString();
    const int attempts =
        recorded == staged.version
            ? settings.value(QLatin1String(source::kKeyUpdatesHandoffAttempts), 0).toInt()
            : 0;
    if (attempts >= reducer::kMaxApplyAttemptsPerVersion) { return false; }

    settings.setValue(QLatin1String(source::kKeyUpdatesHandoffVersion), staged.version);
    settings.setValue(QLatin1String(source::kKeyUpdatesHandoffAttempts), attempts + 1);
    // BEFORE the spawn: a crash between here and CreateProcessW still costs an
    // attempt, which is what bounds a boot loop at two.
    settings.sync();

    // A human-readable copy beside the installer, for support: the registry
    // counter is the authority.
    const QString json = QStringLiteral("{\"count\":%1,\"lastAttemptUtc\":\"%2\"}\n")
                             .arg(attempts + 1)
                             .arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QFile file(staged.dir + QLatin1Char('/') + QLatin1String(kApplyAttemptsName));
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        file.write(json.toUtf8());
        file.close();
    }
    return true;
}

void UpdateHandoff::quarantine(const StagedUpdate& staged) {
    FileStagingStore store;
    store.discard(staged.version);
    // Mute the version through the ordinary skip mechanism, so the next check
    // does not re-download 40 MB of a build this machine has already failed to
    // apply twice. A version that is REQUIRED overrides the skip, which is the
    // one case where retrying is still the right answer.
    QSettings settings{QLatin1String(kSettingsOrganization), QLatin1String(kSettingsApplication)};
    settings.setValue(QLatin1String(source::UpdatePreferenceStore::kKeySkippedVersion),
                      staged.version);
    settings.sync();
}

bool UpdateHandoff::spawnStagedApply(const StagedUpdate& staged, unsigned long pidToWaitOn) {
    const QString targetExe = runningExecutablePath();
    if (targetExe.isEmpty() || staged.exePath.isEmpty()) { return false; }

    FileStagingStore store;
    const QString workingDir = store.root();
    if (workingDir.isEmpty()) { return false; }

    QString commandLine;
    commandLine += QLatin1Char('"');
    commandLine += QDir::toNativeSeparators(staged.exePath);
    commandLine += QLatin1Char('"');
    commandLine += QStringLiteral(" --update-apply --waitpid ");
    commandLine += QString::number(pidToWaitOn);
    commandLine += QStringLiteral(" --target-exe");
    appendQuoted(commandLine, targetExe);
    commandLine += QStringLiteral(" --expect-version ");
    commandLine += staged.version;
    commandLine += QStringLiteral(" --log");
    appendQuoted(commandLine, staged.dir + QLatin1Char('/') + QLatin1String(kApplyLogName));

    std::wstring mutableCommandLine = commandLine.toStdWString();
    const std::wstring nativeCwd = QDir::toNativeSeparators(workingDir).toStdWString();
    const std::wstring nativeExe = QDir::toNativeSeparators(staged.exePath).toStdWString();

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    const BOOL ok = ::CreateProcessW(nativeExe.c_str(), mutableCommandLine.data(), nullptr, nullptr,
                                     FALSE, 0, nullptr, nativeCwd.c_str(), &startup, &process);
    if (ok == FALSE) { return false; }
    ::CloseHandle(process.hThread);
    ::CloseHandle(process.hProcess);
    return true;
}

bool UpdateHandoff::runStartupHandoff(int argc, char** argv) {
    // 1. The loop breaker: the installer relaunches the OLD exe with exactly
    //    this flag after a failed apply, and the troubleshooting docs use it
    //    too. Skip-once semantics: nothing is persisted.
    if (hasNoHandoffFlag(argc, argv)) { return false; }

    // 2. Another instance owns the lifecycle. Skipping here does NOT consume an
    //    attempt: nothing was wrong with the stage.
    if (anotherInstanceRunning()) { return false; }

    const QString currentVersion = QString::fromLatin1(DISH_VERSION);
    auto staged = verifiedStageForApply(currentVersion);
    if (!staged.has_value()) { return false; }

    if (!recordAttempt(*staged)) {
        quarantine(*staged);
        return false;
    }

    if (!spawnStagedApply(*staged, ::GetCurrentProcessId())) {
        // The installer never started, so this boot costs an attempt and the
        // app carries on normally; the next boot retries or quarantines.
        return false;
    }
    return true;
}

} // namespace dish::update
