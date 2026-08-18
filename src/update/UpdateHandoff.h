// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The boot gate and the one spawn path shared with restart-now.
//
// runStartupHandoff() is called from main() IMMEDIATELY after
// dish::crash::install() and BEFORE Winsock, libsodium and QGuiApplication, so
// it may use nothing heavier than QSettings/QFile/QDir/QCryptographicHash and
// Win32. That placement is the point: applying an update while the app's own
// window, sockets and audio stack are still unbuilt is what makes the swap
// invisible, and it is also why the QSettings here names its organization and
// application explicitly (QCoreApplication has not set them yet; the path is
// the same HKCU\Software\TinkerNorth\Dish the preference store uses).
//
// Every guard failure discards the offending stage and returns false, i.e.
// continues a completely normal startup. There is no failure mode in this file
// that can stop the app from launching.

#pragma once

#include "update/UpdatePorts.h"

#include <QString>

#include <optional>

namespace dish::update {

// The hive the boot gate names explicitly, resolving to the same
// HKCU\Software\TinkerNorth\Dish the default QSettings() reaches once
// QGuiApplication has set the organization and application names.
inline constexpr const char* kSettingsOrganization = "TinkerNorth";
inline constexpr const char* kSettingsApplication = "Dish";

// Held by main for the whole process lifetime. A SECOND instance's boot gate
// probes it and skips the handoff without consuming an attempt, which is what
// keeps fast-user-switching and double-launches from fighting over an apply.
// It is a probe, not single-instancing: the second instance runs normally.
class RunningInstanceMutex {
  public:
    RunningInstanceMutex();
    ~RunningInstanceMutex();
    RunningInstanceMutex(const RunningInstanceMutex&) = delete;
    RunningInstanceMutex& operator=(const RunningInstanceMutex&) = delete;
    RunningInstanceMutex(RunningInstanceMutex&&) = delete;
    RunningInstanceMutex& operator=(RunningInstanceMutex&&) = delete;

    bool held() const { return handle_ != nullptr; }

  private:
    void* handle_ = nullptr;
};

class UpdateHandoff {
  public:
    static constexpr const wchar_t* kRunningMutexName = L"Local\\TinkerNorth.Dish.Running";
    static constexpr const char* kNoHandoffFlag = "--no-update-handoff";

    // True when a staged installer was spawned: main must then return 0
    // immediately. The installer (Inno Setup, /OTA mode) waits for this
    // process's Running mutex to clear before it touches a file. False means
    // "carry on starting normally", which is also every failure path.
    static bool runStartupHandoff(int argc, char** argv);

    // The full guard set over the highest surviving stage, in order: version
    // parses, version > DISH_VERSION strictly, attempts below the cap, exe
    // present at the recorded size, and a FULL sha256 re-read that matches the
    // marker. Any failure discards that stage. nullopt means nothing may be
    // applied right now.
    static std::optional<StagedUpdate> verifiedStageForApply(const QString& currentVersion);

    // Increments and SYNCS the per-version attempt counter before the spawn, so
    // a crash between here and CreateProcessW still costs an attempt. False
    // when the cap is already reached (the caller then quarantines).
    static bool recordAttempt(const StagedUpdate& staged);

    // Delete the stage and mute the version, so the next check does not
    // re-download a build this machine has twice failed to apply. The handoff
    // keys are left in place: the coordinator reads them once to surface
    // Failed{ApplyFailed} with the manual download link.
    static void quarantine(const StagedUpdate& staged);

    // The documented switch tail of the apply spawn, in one place so the test
    // suite can pin it against docs/INSTALLER.md: Inno Setup's silent set plus
    // /OTA (wait for the app's mutex, own the relaunch duty) and /LOG into the
    // stage directory.
    static QString applyArguments(const StagedUpdate& staged);

    // CreateProcessW of the staged dish-setup.exe with applyArguments(), cwd =
    // the updates ROOT (never the version directory, so the installer's own
    // directory stays deletable).
    static bool spawnStagedApply(const StagedUpdate& staged);

    // Another dish.exe already owns the update lifecycle.
    static bool anotherInstanceRunning();

    // Absolute path of the running executable, forward slashes.
    static QString runningExecutablePath();
};

} // namespace dish::update
