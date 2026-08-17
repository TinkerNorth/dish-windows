// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The per-phase install reducers and the exact effect ordering each transition
// emits. Tests pin the effect lists in order (journal-before-effect, ARP last,
// cancel windows), so a reordering is a behaviour change, not a cleanup.

#include "installer/InstallMachine.h"

#include "installer/VersionCompare.h"

#include <QDir>
#include <QSet>

namespace dish::installer {

namespace {

Reduction stay() { return Reduction{std::nullopt, {}}; }

template <class T> const T* as(const InstallEvent& e) { return std::get_if<T>(&e); }

QString joined(const QString& base, const QString& rel) { return base + QLatin1Char('/') + rel; }

// Unique parent directories of the manifest paths under `root`, shallow before
// deep so EnsureDir never depends on a later entry. Case-folded dedupe: NTFS
// would fold anyway and one journal line per real directory keeps the replay
// clean.
QStringList parentDirsUnder(const PayloadManifest& manifest, const QString& root) {
    QStringList dirs;
    QSet<QString> seen;
    for (const PayloadEntry& entry : manifest.files) {
        const QStringList parts = entry.path.split(QLatin1Char('/'));
        QString rel;
        for (int i = 0; i < parts.size() - 1; ++i) {
            rel = rel.isEmpty() ? parts.at(i) : rel + QLatin1Char('/') + parts.at(i);
            const QString folded = rel.toCaseFolded();
            if (seen.contains(folded)) { continue; }
            seen.insert(folded);
            dirs.append(joined(root, rel));
        }
    }
    return dirs;
}

void appendCloseEffects(std::vector<InstallEffect>& fx, const InstallPlan& plan,
                        const QVector<ProcInfo>& procs) {
    // Abort policy emits nothing: the UI resolves through CloseAppsRequested
    // and the silent coordinator fails the scan (exit 5).
    if (plan.closePolicy == ClosePolicy::Graceful) {
        fx.push_back(effect::CloseProcesses{procs, false, 10000});
    } else if (plan.closePolicy == ClosePolicy::Force) {
        fx.push_back(effect::CloseProcesses{procs, true, 10000});
    }
}

JournalEntry copiedFileEntry(const QString& destAbs) {
    JournalEntry entry;
    entry.action = JournalAction::CopiedFile;
    entry.path = destAbs;
    return entry;
}

JournalEntry createdDirEntry(const QString& dirAbs) {
    JournalEntry entry;
    entry.action = JournalAction::CreatedDir;
    entry.path = dirAbs;
    return entry;
}

JournalEntry stagedOldEntry(const QString& dir, const QString& rel) {
    JournalEntry entry;
    entry.action = JournalAction::StagedOld;
    entry.path = joined(dir, rel);
    entry.aux = joined(joined(dir, oldDirName()), rel);
    return entry;
}

JournalEntry promotedStagedEntry(const QString& dir, const QString& rel) {
    JournalEntry entry;
    entry.action = JournalAction::PromotedStaged;
    entry.path = joined(dir, rel);
    entry.aux = joined(joined(dir, stageDirName()), rel);
    return entry;
}

JournalEntry shortcutEntry(ShortcutLocation location, Scope scope) {
    JournalEntry entry;
    entry.action = JournalAction::CreatedShortcut;
    entry.aux = shortcutLocationToken(location) + QLatin1Char('|') + scopeToken(scope);
    return entry;
}

ShortcutSpec shortcutSpec(ShortcutLocation location, const InstallPlan& plan) {
    ShortcutSpec spec;
    spec.location = location;
    spec.scope = plan.scope;
    spec.targetAbs = joined(plan.installDir, QStringLiteral("dish.exe"));
    spec.workingDir = plan.installDir;
    spec.iconAbs = spec.targetAbs;
    spec.iconIndex = 0;
    spec.description = QStringLiteral("Dish");
    return spec;
}

// The whole forward pipeline, emitted at Begin (see the header's execution
// model). The coordinator runs it strictly in order; gate effects (ProbeDisk,
// ScanProcesses) pause it through their result events.
std::vector<InstallEffect> buildPipeline(const InstallPlan& plan, const PayloadManifest& manifest) {
    std::vector<InstallEffect> fx;
    const QString dir = plan.installDir;
    const QString copyRoot = plan.isUpgrade ? joined(dir, stageDirName()) : dir;

    fx.push_back(effect::ProbeDisk{dir, manifest.totalBytes + kInstallDiskMargin});
    fx.push_back(effect::ScanProcesses{dir});

    // Directories, journal-before-effect. On a fresh install the install dir
    // itself is journaled (the rollback prunes it when it stays empty); on an
    // upgrade only the stage tree is ours to create.
    fx.push_back(effect::WriteJournalEntry{createdDirEntry(copyRoot)});
    fx.push_back(effect::EnsureDir{copyRoot});
    const QStringList dirs = parentDirsUnder(manifest, copyRoot);
    for (const QString& sub : dirs) {
        fx.push_back(effect::WriteJournalEntry{createdDirEntry(sub)});
        fx.push_back(effect::EnsureDir{sub});
    }

    for (int i = 0; i < manifest.files.size(); ++i) {
        const PayloadEntry& entry = manifest.files.at(i);
        const QString dest = joined(copyRoot, entry.path);
        fx.push_back(effect::WriteJournalEntry{copiedFileEntry(dest)});
        fx.push_back(effect::CopyVerify{i, entry.stagedAs, dest});
    }

    if (plan.isUpgrade) {
        // Commit: re-scan for blockers, then the seconds-long rename window.
        fx.push_back(effect::ScanProcesses{dir});
        // The old recorded manifest backs up first so a rollback restores it.
        fx.push_back(effect::WriteJournalEntry{stagedOldEntry(dir, installedManifestFileName())});
        fx.push_back(effect::StageOld{installedManifestFileName()});
        for (const PayloadEntry& entry : manifest.files) {
            fx.push_back(effect::WriteJournalEntry{stagedOldEntry(dir, entry.path)});
            fx.push_back(effect::StageOld{entry.path});
            fx.push_back(effect::WriteJournalEntry{promotedStagedEntry(dir, entry.path)});
            fx.push_back(effect::PromoteStaged{entry.path});
        }
        // Old-manifest files absent from the new image; the executor owns the
        // enumeration (and journals each rename) because the old manifest is
        // coordinator context.
        fx.push_back(effect::DeleteStale{QString()});
    }

    {
        JournalEntry entry;
        entry.action = JournalAction::WroteManifest;
        entry.path = joined(dir, installedManifestFileName());
        fx.push_back(effect::WriteJournalEntry{entry});
    }
    fx.push_back(effect::WriteInstalledManifest{});

    if (plan.startMenu) {
        fx.push_back(
            effect::WriteJournalEntry{shortcutEntry(ShortcutLocation::StartMenu, plan.scope)});
        fx.push_back(effect::CreateShortcut{shortcutSpec(ShortcutLocation::StartMenu, plan)});
    }
    if (plan.desktop) {
        fx.push_back(
            effect::WriteJournalEntry{shortcutEntry(ShortcutLocation::Desktop, plan.scope)});
        fx.push_back(effect::CreateShortcut{shortcutSpec(ShortcutLocation::Desktop, plan)});
    }

    // ARP is LAST (spec 3.5): a valid entry implies a complete install. The
    // coordinator enriches the journal entry with the previous values on
    // upgrades before persisting it.
    {
        JournalEntry entry;
        entry.action = JournalAction::WroteArp;
        entry.aux = scopeToken(plan.scope);
        fx.push_back(effect::WriteJournalEntry{entry});
    }
    fx.push_back(effect::WriteArp{makeArpValues(plan, manifest.version, manifest.totalBytes)});

    fx.push_back(effect::CommitCleanup{});
    if (plan.launch) { fx.push_back(effect::LaunchApp{true}); }
    fx.push_back(effect::Finish{ExitCode::Ok});
    return fx;
}

Reduction beginInstall(const event::Begin& begin) {
    InstallState next;
    next.phase = InstallPhase::Preflight;
    next.plan = begin.plan;
    next.bytesTotal = begin.manifest.totalBytes;
    return Reduction{std::move(next), buildPipeline(begin.plan, begin.manifest)};
}

Reduction fail(const InstallState& state, SetupError error, const QString& path) {
    InstallState next = state;
    next.phase = InstallPhase::Failed;
    next.error = error;
    next.errorPath = path;
    return Reduction{std::move(next), {effect::Finish{toExitCode(error)}}};
}

// EffectFail routing shared by every executing phase: anything journaled means
// RollingBack (the coordinator replays its journal mirror in reverse and feeds
// RollbackStepDone back); a clean slate fails straight through.
Reduction failOrRollBack(const InstallState& state, const event::EffectFail& e) {
    if (state.rollbackCursor > 0) {
        InstallState next = state;
        next.phase = InstallPhase::RollingBack;
        next.error = e.error;
        next.errorPath = e.path;
        return Reduction{std::move(next), {}};
    }
    return fail(state, e.error, e.path);
}

Reduction toElevation(const InstallState& state) {
    InstallState next = state;
    next.phase = InstallPhase::AwaitingElevation;
    return Reduction{std::move(next), {effect::RelaunchElevated{QString(), QStringList()}}};
}

Reduction reduceIdle(const InstallState& state, const InstallEvent& event) {
    if (const auto* begin = as<event::Begin>(event)) { return beginInstall(*begin); }
    if (as<event::ElevationRequested>(event) != nullptr) { return toElevation(state); }
    return stay();
}

Reduction reducePreflight(const InstallState& state, const InstallEvent& event) {
    if (as<event::PreflightOk>(event) != nullptr) { return stay(); }
    if (const auto* preflightFail = as<event::PreflightFail>(event)) {
        return fail(state, preflightFail->error, state.plan.installDir);
    }
    if (const auto* found = as<event::BlockersFound>(event)) {
        InstallState next = state;
        next.phase = InstallPhase::AwaitingBlockers;
        Reduction r{std::move(next), {}};
        appendCloseEffects(r.effects, state.plan, found->procs);
        return r;
    }
    if (as<event::BlockersGone>(event) != nullptr) {
        InstallState next = state;
        next.phase = InstallPhase::Copying;
        return Reduction{std::move(next), {}};
    }
    if (as<event::ElevationRequested>(event) != nullptr) { return toElevation(state); }
    if (const auto* effectFail = as<event::EffectFail>(event)) {
        return failOrRollBack(state, *effectFail);
    }
    if (as<event::CancelRequested>(event) != nullptr) {
        return fail(state, SetupError::Cancelled, QString());
    }
    return stay();
}

Reduction reduceAwaitingBlockers(const InstallState& state, const InstallEvent& event) {
    if (const auto* close = as<event::CloseAppsRequested>(event)) {
        return Reduction{std::nullopt,
                         {effect::CloseProcesses{QVector<ProcInfo>(), close->force, 10000}}};
    }
    if (as<event::BlockersFound>(event) != nullptr) {
        // A graceful close attempt did not clear the set; the UI swaps its
        // dialog to Force / Retry, so no automatic re-close here (it would
        // loop).
        return stay();
    }
    if (as<event::BlockersGone>(event) != nullptr) {
        InstallState next = state;
        next.phase = InstallPhase::Copying;
        return Reduction{std::move(next), {}};
    }
    if (as<event::ElevationRequested>(event) != nullptr) { return toElevation(state); }
    if (const auto* effectFail = as<event::EffectFail>(event)) {
        return failOrRollBack(state, *effectFail);
    }
    if (as<event::CancelRequested>(event) != nullptr) {
        return fail(state, SetupError::Cancelled, QString());
    }
    return stay();
}

Reduction reduceAwaitingElevation(const InstallState& state, const InstallEvent& event) {
    if (as<event::ElevationSpawned>(event) != nullptr) {
        // The elevated instance owns the install from here; this one reports
        // success and quits (the controller suppresses the Done page).
        InstallState next = state;
        next.phase = InstallPhase::Done;
        return Reduction{std::move(next), {effect::Finish{ExitCode::Ok}}};
    }
    if (as<event::ElevationDeclined>(event) != nullptr || as<event::EffectFail>(event) != nullptr) {
        // Declined UAC re-arms the wizard (the ElevationFace, via the
        // coordinator's elevationDeclined signal); never a prompt loop.
        InstallState next = state;
        next.phase = InstallPhase::Idle;
        next.error = SetupError::None;
        next.errorPath.clear();
        return Reduction{std::move(next), {}};
    }
    return stay();
}

Reduction reduceCopying(const InstallState& state, const InstallEvent& event) {
    if (const auto* ok = as<event::EffectOk>(event)) {
        if (ok->effectTag == effecttag::writeJournalEntry) {
            InstallState next = state;
            next.rollbackCursor += 1;
            return Reduction{std::move(next), {}};
        }
        if (ok->effectTag == effecttag::copyVerify) {
            InstallState next = state;
            next.fileIndex = ok->index + 1;
            return Reduction{std::move(next), {}};
        }
        if (ok->effectTag == effecttag::writeInstalledManifest) {
            // Fresh installs have no commit window: the manifest write is the
            // first Finalizing act (spec D12's no-op pass-through).
            InstallState next = state;
            next.phase = InstallPhase::Finalizing;
            return Reduction{std::move(next), {}};
        }
        return stay();
    }
    if (const auto* progress = as<event::CopyProgress>(event)) {
        InstallState next = state;
        next.bytesDone += progress->bytesDelta;
        return Reduction{std::move(next), {}};
    }
    if (const auto* found = as<event::BlockersFound>(event)) {
        // Only the upgrade pipeline scans mid-run: the commit gate.
        InstallState next = state;
        next.phase = InstallPhase::Committing;
        Reduction r{std::move(next), {}};
        appendCloseEffects(r.effects, state.plan, found->procs);
        return r;
    }
    if (as<event::BlockersGone>(event) != nullptr) {
        InstallState next = state;
        next.phase = InstallPhase::Committing;
        return Reduction{std::move(next), {}};
    }
    if (as<event::CancelRequested>(event) != nullptr) {
        // Recorded here; the coordinator aborts the in-flight copy, which
        // surfaces as EffectFail{Cancelled} and rolls back.
        InstallState next = state;
        next.cancelRequested = true;
        return Reduction{std::move(next), {}};
    }
    if (const auto* effectFail = as<event::EffectFail>(event)) {
        return failOrRollBack(state, *effectFail);
    }
    return stay();
}

Reduction reduceCommitting(const InstallState& state, const InstallEvent& event) {
    if (const auto* ok = as<event::EffectOk>(event)) {
        if (ok->effectTag == effecttag::writeJournalEntry) {
            InstallState next = state;
            next.rollbackCursor += 1;
            return Reduction{std::move(next), {}};
        }
        if (ok->effectTag == effecttag::writeInstalledManifest) {
            InstallState next = state;
            next.phase = InstallPhase::Finalizing;
            return Reduction{std::move(next), {}};
        }
        return stay();
    }
    if (const auto* close = as<event::CloseAppsRequested>(event)) {
        return Reduction{std::nullopt,
                         {effect::CloseProcesses{QVector<ProcInfo>(), close->force, 10000}}};
    }
    if (as<event::BlockersFound>(event) != nullptr) { return stay(); }
    if (as<event::BlockersGone>(event) != nullptr) { return stay(); }
    if (as<event::CancelRequested>(event) != nullptr) {
        return stay(); // cancel is ignored during the commit window (spec 3.5)
    }
    if (const auto* effectFail = as<event::EffectFail>(event)) {
        return failOrRollBack(state, *effectFail);
    }
    return stay();
}

Reduction reduceFinalizing(const InstallState& state, const InstallEvent& event) {
    if (const auto* ok = as<event::EffectOk>(event)) {
        if (ok->effectTag == effecttag::writeJournalEntry) {
            InstallState next = state;
            next.rollbackCursor += 1;
            return Reduction{std::move(next), {}};
        }
        if (ok->effectTag == effecttag::finish) {
            InstallState next = state;
            next.phase = InstallPhase::Done;
            return Reduction{std::move(next), {}};
        }
        return stay();
    }
    if (as<event::CancelRequested>(event) != nullptr) { return stay(); }
    if (const auto* effectFail = as<event::EffectFail>(event)) {
        return failOrRollBack(state, *effectFail);
    }
    return stay();
}

Reduction reduceRollingBack(const InstallState& state, const InstallEvent& event) {
    if (const auto* done = as<event::RollbackStepDone>(event)) {
        InstallState next = state;
        next.rollbackCursor = done->index;
        if (!done->ok) { next.error = SetupError::RollbackIncomplete; }
        if (done->index == 0) {
            next.phase = InstallPhase::Failed;
            const ExitCode code = next.error == SetupError::RollbackIncomplete
                                      ? ExitCode::RollbackIncomplete
                                      : exitAfterRollback(next.error);
            return Reduction{std::move(next), {effect::Finish{code}}};
        }
        return Reduction{std::move(next), {}};
    }
    // Cancel and every straggler ack are ignored: the replay must finish.
    return stay();
}

Reduction reduceFailed(const InstallState& state, const InstallEvent& event) {
    Q_UNUSED(state);
    if (const auto* begin = as<event::Begin>(event)) {
        return beginInstall(*begin); // retry() re-arms with a fresh plan
    }
    return stay();
}

} // namespace

Reduction reduce(const InstallState& state, const InstallEvent& event) {
    switch (state.phase) {
    case InstallPhase::Idle:
        return reduceIdle(state, event);
    case InstallPhase::Preflight:
        return reducePreflight(state, event);
    case InstallPhase::AwaitingBlockers:
        return reduceAwaitingBlockers(state, event);
    case InstallPhase::AwaitingElevation:
        return reduceAwaitingElevation(state, event);
    case InstallPhase::Copying:
        return reduceCopying(state, event);
    case InstallPhase::Committing:
        return reduceCommitting(state, event);
    case InstallPhase::Finalizing:
        return reduceFinalizing(state, event);
    case InstallPhase::RollingBack:
        return reduceRollingBack(state, event);
    case InstallPhase::Done:
        return stay();
    case InstallPhase::Failed:
        return reduceFailed(state, event);
    }
    return stay();
}

ArpValues makeArpValues(const InstallPlan& plan, const QString& version, qint64 totalBytes) {
    ArpValues values;
    const QString native = QDir::toNativeSeparators(QDir::cleanPath(plan.installDir));
    values.displayName = QStringLiteral("Dish");
    values.displayVersion = version;
    if (const auto parsed = parseSemVer(version)) {
        values.versionMajor = parsed->major;
        values.versionMinor = parsed->minor;
    }
    values.publisher = QStringLiteral("TinkerNorth");
    values.displayIcon = native + QStringLiteral("\\dish.exe,0");
    values.installLocation = native;
    values.installDate = QString(); // stamped by RegistryOps at write time
    values.uninstallString = QLatin1Char('"') + native + QStringLiteral("\\uninstall.exe\"");
    values.quietUninstallString = values.uninstallString + QStringLiteral(" --silent");
    values.estimatedSizeKiB =
        static_cast<quint32>((totalBytes + 1023) / 1024); // ceil, KiB (spec 10)
    values.urlInfoAbout = QStringLiteral("https://github.com/TinkerNorth/dish-windows");
    values.helpLink = values.urlInfoAbout;
    values.installScope = scopeToken(plan.scope);
    return values;
}

ExitCode exitAfterRollback(SetupError error) {
    // toExitCode already collapses the op-failure family onto RolledBack; the
    // named wrapper exists so call sites say what they mean and tests pin the
    // rule.
    return toExitCode(error);
}

} // namespace dish::installer
