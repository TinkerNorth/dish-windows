// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pure install FSM (spec 3.5), cloned from the src/core/reducer/
// UsbPathMachine.h house shape: variant events in `namespace event`, effects
// in `namespace effect`, everything equality-comparable, `reduce` total and
// IO-free. The coordinator turns world changes into events, runs reduce, and
// executes the returned effects on its worker thread.
//
// Execution model (binding for the coordinator and pinned by tests): the
// InstallState struct is fixed by the spec and cannot carry the manifest, so
// event::Begin is the ONE moment the reducer sees per-file data. reduce(Idle,
// Begin) therefore returns the ENTIRE forward pipeline as an ordered effect
// list; the coordinator executes it strictly sequentially, feeding each
// outcome back through reduce. Gating phases (AwaitingBlockers,
// AwaitingElevation) pause the queue; RollingBack, Done and Failed drop it.
// Rollback is replayed by the coordinator from its journal mirror in REVERSE
// entry order (materialized as effect::RollbackStep executions), because the
// fixed state cannot hold the entries either; the reducer sequences the
// replay through event::RollbackStepDone and `rollbackCursor`, which during
// forward progress counts journaled entries.
//
// Ordering invariants reduce() encodes (spec 3.5): every WriteJournalEntry
// precedes the destructive effect it covers; WriteArp is the LAST mutating
// step before CommitCleanup, so a valid ARP entry implies a complete install;
// Cancel is ignored during Committing/Finalizing/RollingBack; every EffectFail
// outside RollingBack lands in RollingBack when anything was journaled and in
// Failed otherwise; RollbackStepDone{ok=false} accumulates into
// RollbackIncomplete (exit 9 instead of 8).

#pragma once

#include "installer/Errors.h"
#include "installer/InstallPlan.h"
#include "installer/Journal.h"
#include "installer/Manifest.h"
#include "installer/ops/ProcessOps.h"
#include "installer/ops/RegistryOps.h"
#include "installer/ops/ShortcutOps.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>
#include <variant>
#include <vector>

namespace dish::installer {

enum class InstallPhase {
    Idle,
    Preflight,
    AwaitingBlockers,
    AwaitingElevation,
    Copying,
    Committing,
    Finalizing,
    RollingBack,
    Done,
    Failed,
};

struct InstallState {
    InstallPhase phase = InstallPhase::Idle;
    InstallPlan plan;
    int fileIndex = 0;
    qint64 bytesDone = 0, bytesTotal = 0;
    SetupError error = SetupError::None;
    QString errorPath;
    bool cancelRequested = false;
    int rollbackCursor = 0;

    bool operator==(const InstallState& o) const {
        return phase == o.phase && plan == o.plan && fileIndex == o.fileIndex &&
               bytesDone == o.bytesDone && bytesTotal == o.bytesTotal && error == o.error &&
               errorPath == o.errorPath && cancelRequested == o.cancelRequested &&
               rollbackCursor == o.rollbackCursor;
    }
    bool operator!=(const InstallState& o) const { return !(*this == o); }
};

// Tags carried by EffectOk/EffectFail so acknowledgements route without RTTI
// over the variant. Shared with UninstallMachine (its tags continue the run).
namespace effecttag {
inline constexpr int probeDisk = 1;
inline constexpr int scanProcesses = 2;
inline constexpr int closeProcesses = 3;
inline constexpr int relaunchElevated = 4;
inline constexpr int writeJournalEntry = 5;
inline constexpr int ensureDir = 6;
inline constexpr int copyVerify = 7;
inline constexpr int stageOld = 8;
inline constexpr int promoteStaged = 9;
inline constexpr int deleteStale = 10;
inline constexpr int writeInstalledManifest = 11;
inline constexpr int createShortcut = 12;
inline constexpr int writeArp = 13;
inline constexpr int commitCleanup = 14;
inline constexpr int rollbackStep = 15;
inline constexpr int launchApp = 16;
inline constexpr int finish = 17;
// uninstall-only
inline constexpr int removeShortcut = 18;
inline constexpr int removeFile = 19;
inline constexpr int removeResidue = 20;
inline constexpr int removeUpdatesCache = 21;
inline constexpr int purgeUserData = 22;
inline constexpr int pruneDirs = 23;
inline constexpr int spawnHelper = 24;
} // namespace effecttag

// ── Events ────────────────────────────────────────────────────────────────

namespace event {

struct Begin {
    InstallPlan plan;
    PayloadManifest manifest;
    bool operator==(const Begin& o) const { return plan == o.plan && manifest == o.manifest; }
};
struct PreflightOk {
    qint64 freeBytes = 0;
    bool operator==(const PreflightOk& o) const { return freeBytes == o.freeBytes; }
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
struct ElevationRequested {
    bool operator==(const ElevationRequested&) const { return true; }
};
struct ElevationSpawned {
    bool operator==(const ElevationSpawned&) const { return true; }
};
struct ElevationDeclined {
    bool operator==(const ElevationDeclined&) const { return true; }
};
struct EffectOk {
    int effectTag = 0;
    int index = 0; // manifestIndex for copyVerify, journal index for rollbackStep, else 0
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
struct CopyProgress {
    qint64 bytesDelta = 0;
    bool operator==(const CopyProgress& o) const { return bytesDelta == o.bytesDelta; }
};
struct CancelRequested {
    bool operator==(const CancelRequested&) const { return true; }
};
struct RollbackStepDone {
    int index = 0;
    bool ok = true;
    bool operator==(const RollbackStepDone& o) const { return index == o.index && ok == o.ok; }
};

} // namespace event

using InstallEvent =
    std::variant<event::Begin, event::PreflightOk, event::PreflightFail, event::BlockersFound,
                 event::BlockersGone, event::CloseAppsRequested, event::ElevationRequested,
                 event::ElevationSpawned, event::ElevationDeclined, event::EffectOk,
                 event::EffectFail, event::CopyProgress, event::CancelRequested,
                 event::RollbackStepDone>;

// ── Effects (returned as data; executed by the coordinator) ─────────────────

namespace effect {

struct ProbeDisk {
    QString dir;
    qint64 requiredBytes = 0;
    bool operator==(const ProbeDisk& o) const {
        return dir == o.dir && requiredBytes == o.requiredBytes;
    }
};
struct ScanProcesses {
    QString dir;
    bool operator==(const ScanProcesses& o) const { return dir == o.dir; }
};
// Empty `procs` means "the set current at execution time": the executor
// re-snapshots before closing, which also closes the TOCTOU between the scan
// and the close.
struct CloseProcesses {
    QVector<ProcInfo> procs;
    bool force = false;
    int graceMs = 0;
    bool operator==(const CloseProcesses& o) const {
        return procs == o.procs && force == o.force && graceMs == o.graceMs;
    }
};
// Empty exe/argv: the coordinator substitutes the recorded original
// dish-setup.exe and the serialized current options (the reducer never learns
// machine-local paths).
struct RelaunchElevated {
    QString exe;
    QStringList argv;
    bool operator==(const RelaunchElevated& o) const { return exe == o.exe && argv == o.argv; }
};
struct WriteJournalEntry {
    JournalEntry entry;
    bool operator==(const WriteJournalEntry& o) const { return entry == o.entry; }
};
struct EnsureDir {
    QString abs;
    bool operator==(const EnsureDir& o) const { return abs == o.abs; }
};
// `fromAbs` is the image-relative source name (the entry's stagedAs); the
// executor prefixes the extracted-image staging dir, which the reducer never
// sees. `toAbs` is the absolute destination: final path on fresh installs,
// under `<dir>/.dish-stage/` on upgrades (spec D12).
struct CopyVerify {
    int manifestIndex = 0;
    QString fromAbs;
    QString toAbs;
    bool operator==(const CopyVerify& o) const {
        return manifestIndex == o.manifestIndex && fromAbs == o.fromAbs && toAbs == o.toAbs;
    }
};
struct StageOld {
    QString relPath; // upgrade commit: final -> .dish-old
    bool operator==(const StageOld& o) const { return relPath == o.relPath; }
};
struct PromoteStaged {
    QString relPath; // upgrade commit: .dish-stage -> final
    bool operator==(const PromoteStaged& o) const { return relPath == o.relPath; }
};
// Empty relPath = "every file in the OLD manifest absent from the new one";
// the old manifest is coordinator context, so the executor enumerates and
// journals each rename into `.dish-old` itself.
struct DeleteStale {
    QString relPath;
    bool operator==(const DeleteStale& o) const { return relPath == o.relPath; }
};
struct WriteInstalledManifest {
    bool operator==(const WriteInstalledManifest&) const { return true; }
};
struct CreateShortcut {
    ShortcutSpec spec;
    bool operator==(const CreateShortcut& o) const { return spec == o.spec; }
};
struct WriteArp {
    ArpValues values;
    bool operator==(const WriteArp& o) const { return values == o.values; }
};
struct CommitCleanup { // delete .dish-old, .dish-stage, journal
    bool operator==(const CommitCleanup&) const { return true; }
};
struct RollbackStep {
    JournalEntry entry;
    bool operator==(const RollbackStep& o) const { return entry == o.entry; }
};
struct LaunchApp {
    bool deElevate = true;
    bool operator==(const LaunchApp& o) const { return deElevate == o.deElevate; }
};
struct Finish {
    ExitCode code = ExitCode::Ok;
    bool operator==(const Finish& o) const { return code == o.code; }
};

} // namespace effect

using InstallEffect =
    std::variant<effect::ProbeDisk, effect::ScanProcesses, effect::CloseProcesses,
                 effect::RelaunchElevated, effect::WriteJournalEntry, effect::EnsureDir,
                 effect::CopyVerify, effect::StageOld, effect::PromoteStaged, effect::DeleteStale,
                 effect::WriteInstalledManifest, effect::CreateShortcut, effect::WriteArp,
                 effect::CommitCleanup, effect::RollbackStep, effect::LaunchApp, effect::Finish>;

// next == nullopt means "state unchanged" (this machine never removes itself).
struct Reduction {
    std::optional<InstallState> next;
    std::vector<InstallEffect> effects;
};

Reduction reduce(const InstallState& state, const InstallEvent& event);

// The extra free space demanded beyond manifest totalBytes, absorbing journal,
// manifest and filesystem overhead (fresh installs write totals; upgrades
// stage totals while the old files are renamed, not duplicated).
inline constexpr qint64 kInstallDiskMargin = 64ll * 1024 * 1024;

// The ARP values for a completed install of `version` into `plan.installDir`
// (spec section 10). Pure: installDate stays empty and Win32RegistryOps stamps
// today's yyyyMMdd at write time. Shared by the reducer and by the
// coordinator's prev-ARP journal enrichment on upgrades.
ArpValues makeArpValues(const InstallPlan& plan, const QString& version, qint64 totalBytes);

// The exit code a rollback triggered by `error` reports once the replay
// completed cleanly: op failures collapse to RolledBack (the exit reports the
// outcome, not the trigger), everything else keeps its own code.
ExitCode exitAfterRollback(SetupError error);

} // namespace dish::installer
