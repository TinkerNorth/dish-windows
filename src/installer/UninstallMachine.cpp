// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The per-phase uninstall reducers. Tests pin the pipeline order (shortcuts,
// files, residue, updates cache, purge, prune, helper handoff, finish), so a
// reordering is a behaviour change.

#include "installer/UninstallMachine.h"

#include "installer/InstallPlan.h"

namespace dish::installer {

namespace {

UninstallReduction stay() { return UninstallReduction{std::nullopt, {}}; }

template <class T> const T* as(const UninstallEvent& e) { return std::get_if<T>(&e); }

void appendCloseEffects(std::vector<UninstallEffect>& fx, ClosePolicy policy,
                        const QVector<ProcInfo>& procs) {
    if (policy == ClosePolicy::Graceful) {
        fx.push_back(ueffect::CloseProcesses{procs, false, 10000});
    } else if (policy == ClosePolicy::Force) {
        fx.push_back(ueffect::CloseProcesses{procs, true, 10000});
    }
}

// The whole forward pipeline (spec 11.1 steps 4-7), emitted at Begin and run
// strictly in order by the coordinator.
std::vector<UninstallEffect> buildPipeline(const InstalledManifest& manifest, bool purgeUserData) {
    std::vector<UninstallEffect> fx;
    const QString dir = manifest.installDir;

    fx.push_back(ueffect::ScanProcesses{dir});
    for (const QString& link : manifest.shortcutPaths) {
        fx.push_back(ueffect::RemoveShortcut{link});
    }
    for (const PayloadEntry& entry : manifest.files) {
        fx.push_back(ueffect::RemoveFile{dir + QLatin1Char('/') + entry.path});
    }
    fx.push_back(ueffect::RemoveResidue{dir});
    fx.push_back(ueffect::RemoveUpdatesCache{});
    if (purgeUserData) { fx.push_back(ueffect::PurgeUserData{}); }
    fx.push_back(ueffect::PruneDirs{dir});
    fx.push_back(ueffect::SpawnHelper{QString(), QStringList()});
    fx.push_back(ueffect::Finish{ExitCode::Ok});
    return fx;
}

UninstallReduction fail(const UninstallState& state, SetupError error, const QString& path) {
    UninstallState next = state;
    next.phase = UninstallPhase::Failed;
    next.error = error;
    next.errorPath = path;
    return UninstallReduction{std::move(next), {ueffect::Finish{toExitCode(error)}}};
}

// The monotone phase an acknowledged effect implies; the sequential queue
// guarantees these only ever move forward.
UninstallPhase phaseForTag(int tag, UninstallPhase current) {
    switch (tag) {
    case effecttag::removeShortcut:
        return UninstallPhase::RemovingShortcuts;
    case effecttag::removeFile:
    case effecttag::removeResidue:
    case effecttag::removeUpdatesCache:
    case effecttag::pruneDirs:
        return UninstallPhase::RemovingFiles;
    case effecttag::purgeUserData:
        return UninstallPhase::PurgingData;
    case effecttag::spawnHelper:
        return UninstallPhase::HandingOff;
    case effecttag::finish:
        return UninstallPhase::Done;
    default:
        return current;
    }
}

UninstallReduction reduceRemoval(const UninstallState& state, const UninstallEvent& event) {
    if (const auto* ok = as<uevent::EffectOk>(event)) {
        UninstallState next = state;
        next.phase = phaseForTag(ok->effectTag, state.phase);
        if (ok->effectTag == effecttag::removeFile) { next.fileIndex = ok->index + 1; }
        if (next == state) { return stay(); }
        return UninstallReduction{std::move(next), {}};
    }
    if (const auto* effectFail = as<uevent::EffectFail>(event)) {
        // File-level trouble never reaches here (the executor defers those to
        // the helper); what does — a helper that cannot spawn, an unreadable
        // manifest mid-run — is unrecoverable forward damage. No rollback
        // exists for an uninstall; report and stop.
        return fail(state, effectFail->error, effectFail->path);
    }
    // Cancel is ignored once removal started: half an uninstall must finish
    // (the UI hides Cancel from this page).
    return stay();
}

} // namespace

UninstallReduction reduce(const UninstallState& state, const UninstallEvent& event) {
    switch (state.phase) {
    case UninstallPhase::Idle: {
        if (const auto* begin = as<uevent::Begin>(event)) {
            UninstallState next;
            next.phase = UninstallPhase::Preflight;
            next.installDir = begin->manifest.installDir;
            next.scope = begin->manifest.scope;
            next.purgeUserData = begin->purgeUserData;
            next.fileCount = begin->manifest.files.size();
            return UninstallReduction{std::move(next),
                                      buildPipeline(begin->manifest, begin->purgeUserData)};
        }
        return stay();
    }
    case UninstallPhase::Preflight: {
        if (as<uevent::PreflightOk>(event) != nullptr) { return stay(); }
        if (const auto* preflightFail = as<uevent::PreflightFail>(event)) {
            return fail(state, preflightFail->error, state.installDir);
        }
        if (const auto* found = as<uevent::BlockersFound>(event)) {
            UninstallState next = state;
            next.phase = UninstallPhase::AwaitingBlockers;
            UninstallReduction r{std::move(next), {}};
            // The coordinator carries the close policy for silent runs; the
            // UI resolves through CloseAppsRequested. Policy effects mirror
            // the install machine and are appended by the coordinator's
            // policy, which it injects via CloseAppsRequested — here only the
            // phase moves.
            Q_UNUSED(found);
            return r;
        }
        if (as<uevent::BlockersGone>(event) != nullptr) {
            UninstallState next = state;
            next.phase = UninstallPhase::RemovingShortcuts;
            return UninstallReduction{std::move(next), {}};
        }
        if (const auto* effectFail = as<uevent::EffectFail>(event)) {
            return fail(state, effectFail->error, effectFail->path);
        }
        if (as<uevent::CancelRequested>(event) != nullptr) {
            return fail(state, SetupError::Cancelled, QString());
        }
        return stay();
    }
    case UninstallPhase::AwaitingBlockers: {
        if (const auto* close = as<uevent::CloseAppsRequested>(event)) {
            std::vector<UninstallEffect> fx;
            appendCloseEffects(fx, close->force ? ClosePolicy::Force : ClosePolicy::Graceful,
                               QVector<ProcInfo>());
            return UninstallReduction{std::nullopt, std::move(fx)};
        }
        if (as<uevent::BlockersFound>(event) != nullptr) { return stay(); }
        if (as<uevent::BlockersGone>(event) != nullptr) {
            UninstallState next = state;
            next.phase = UninstallPhase::RemovingShortcuts;
            return UninstallReduction{std::move(next), {}};
        }
        if (const auto* effectFail = as<uevent::EffectFail>(event)) {
            return fail(state, effectFail->error, effectFail->path);
        }
        if (as<uevent::CancelRequested>(event) != nullptr) {
            return fail(state, SetupError::Cancelled, QString());
        }
        return stay();
    }
    case UninstallPhase::RemovingShortcuts:
    case UninstallPhase::RemovingFiles:
    case UninstallPhase::PurgingData:
    case UninstallPhase::HandingOff:
        return reduceRemoval(state, event);
    case UninstallPhase::Done:
    case UninstallPhase::Failed:
        return stay();
    }
    return stay();
}

} // namespace dish::installer
