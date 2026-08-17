// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pure uninstall FSM (spec 3.6), mirroring InstallMachine's shape and
// execution model: uevent::Begin carries the recorded InstalledManifest (the
// only per-file data the reducer ever sees) and returns the whole forward
// pipeline; the coordinator executes it sequentially and feeds outcomes back.
// Uninstall is forward-only best-effort — there is no rollback: files the
// engine cannot delete (working set, sharing violations) are appended to the
// helper's leftover list by the executor instead of failing, and the helper
// finishes after this process exits (ARP key LAST, spec D2/11.1).
//
// Ordering invariants (pinned by tests): shortcuts before files; files before
// residue/updates-cache/purge; purge only when requested; SpawnHelper after
// everything else and before Finish; the engine never deletes a file it did
// not record (the reducer only ever emits manifest-listed paths).

#pragma once

#include "installer/Errors.h"
#include "installer/InstallMachine.h"
#include "installer/Manifest.h"
#include "installer/ops/ProcessOps.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <variant>
#include <vector>

namespace dish::installer {

enum class UninstallPhase {
    Idle,
    Preflight,
    AwaitingBlockers,
    RemovingShortcuts,
    RemovingFiles,
    PurgingData,
    HandingOff,
    Done,
    Failed,
};

struct UninstallState {
    UninstallPhase phase = UninstallPhase::Idle;
    QString installDir;
    QString scope; // "user" | "machine" (manifest token; the helper's --arp)
    bool purgeUserData = false;
    int fileIndex = 0;
    int fileCount = 0;
    SetupError error = SetupError::None;
    QString errorPath;
    bool cancelRequested = false;

    bool operator==(const UninstallState& o) const {
        return phase == o.phase && installDir == o.installDir && scope == o.scope &&
               purgeUserData == o.purgeUserData && fileIndex == o.fileIndex &&
               fileCount == o.fileCount && error == o.error && errorPath == o.errorPath &&
               cancelRequested == o.cancelRequested;
    }
    bool operator!=(const UninstallState& o) const { return !(*this == o); }
};

// ── Events (uevent:: so both machines can be included together) ────────────

namespace uevent {

struct Begin {
    InstalledManifest manifest;
    bool purgeUserData = false;
    bool operator==(const Begin& o) const {
        return manifest == o.manifest && purgeUserData == o.purgeUserData;
    }
};
struct PreflightOk {
    bool operator==(const PreflightOk&) const { return true; }
};
struct PreflightFail {
    SetupError error = SetupError::Internal;
    bool operator==(const PreflightFail& o) const { return error == o.error; }
};
struct BlockersFound {
    QVector<ProcInfo> procs;
    bool operator==(const BlockersFound& o) const { return procs == o.procs; }
};
struct BlockersGone {
    bool operator==(const BlockersGone&) const { return true; }
};
struct CloseAppsRequested {
    bool force = false;
    bool operator==(const CloseAppsRequested& o) const { return force == o.force; }
};
struct EffectOk {
    int effectTag = 0;
    int index = 0;
    bool operator==(const EffectOk& o) const {
        return effectTag == o.effectTag && index == o.index;
    }
};
struct EffectFail {
    int effectTag = 0;
    SetupError error = SetupError::Internal;
    QString path;
    bool operator==(const EffectFail& o) const {
        return effectTag == o.effectTag && error == o.error && path == o.path;
    }
};
struct CancelRequested {
    bool operator==(const CancelRequested&) const { return true; }
};

} // namespace uevent

using UninstallEvent =
    std::variant<uevent::Begin, uevent::PreflightOk, uevent::PreflightFail, uevent::BlockersFound,
                 uevent::BlockersGone, uevent::CloseAppsRequested, uevent::EffectOk,
                 uevent::EffectFail, uevent::CancelRequested>;

// ── Effects (ueffect::; tags from installer::effecttag) ────────────────────

namespace ueffect {

struct ScanProcesses {
    QString dir;
    bool operator==(const ScanProcesses& o) const { return dir == o.dir; }
};
struct CloseProcesses {
    QVector<ProcInfo> procs;
    bool force = false;
    int graceMs = 0;
    bool operator==(const CloseProcesses& o) const {
        return procs == o.procs && force == o.force && graceMs == o.graceMs;
    }
};
struct RemoveShortcut {
    QString linkAbs;
    bool operator==(const RemoveShortcut& o) const { return linkAbs == o.linkAbs; }
};
// The executor skips working-set members (own exe, helper, mapped DLLs) and
// sharing-violation survivors, appending them to the helper list instead of
// failing (spec 3.6).
struct RemoveFile {
    QString abs;
    bool operator==(const RemoveFile& o) const { return abs == o.abs; }
};
// .dish-manifest.json, `.dish-old`, `.dish-stage` and journal leftovers.
struct RemoveResidue {
    QString dir;
    bool operator==(const RemoveResidue& o) const { return dir == o.dir; }
};
// %LOCALAPPDATA%\Dish\updates — unconditional, the cache is not user data
// (spec D13).
struct RemoveUpdatesCache {
    bool operator==(const RemoveUpdatesCache&) const { return true; }
};
// HKCU\Software\Dish\Dish, HKCU\Software\TinkerNorth\Dish, %LOCALAPPDATA%\Dish
// for the invoking user only. Best-effort.
struct PurgeUserData {
    bool operator==(const PurgeUserData&) const { return true; }
};
// Now-empty manifest directories, bottom-up.
struct PruneDirs {
    QString dir;
    bool operator==(const PruneDirs& o) const { return dir == o.dir; }
};
// Empty fields = resolved by the executor (temp copy of uninstall-helper.exe,
// leftover list file, own pid); the reducer never learns machine-local paths.
struct SpawnHelper {
    QString helperTempPath;
    QStringList argv;
    bool operator==(const SpawnHelper& o) const {
        return helperTempPath == o.helperTempPath && argv == o.argv;
    }
};
struct Finish {
    ExitCode code = ExitCode::Ok;
    bool operator==(const Finish& o) const { return code == o.code; }
};

} // namespace ueffect

using UninstallEffect =
    std::variant<ueffect::ScanProcesses, ueffect::CloseProcesses, ueffect::RemoveShortcut,
                 ueffect::RemoveFile, ueffect::RemoveResidue, ueffect::RemoveUpdatesCache,
                 ueffect::PurgeUserData, ueffect::PruneDirs, ueffect::SpawnHelper, ueffect::Finish>;

// next == nullopt means "state unchanged".
struct UninstallReduction {
    std::optional<UninstallState> next;
    std::vector<UninstallEffect> effects;
};

UninstallReduction reduce(const UninstallState& state, const UninstallEvent& event);

} // namespace dish::installer
