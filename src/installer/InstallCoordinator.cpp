// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/InstallCoordinator.h"

#include "installer/ops/KnownFolders.h"
#include "installer/ops/Win32FileOps.h"

#include <QDateTime>
#include <QDir>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <functional>

namespace dish::installer {

namespace {

bool isGateEffect(const InstallEffect& effect) {
    return std::holds_alternative<effect::ScanProcesses>(effect) ||
           std::holds_alternative<effect::CloseProcesses>(effect) ||
           std::holds_alternative<effect::RelaunchElevated>(effect);
}

bool isExecutingPhase(InstallPhase phase) {
    return phase == InstallPhase::Preflight || phase == InstallPhase::Copying ||
           phase == InstallPhase::Committing || phase == InstallPhase::Finalizing ||
           phase == InstallPhase::RollingBack;
}

QString parentOf(const QString& abs) {
    const int slash = abs.lastIndexOf(QLatin1Char('/'));
    return slash > 0 ? abs.left(slash) : abs;
}

// Names for the one failure line dispatch() writes. EffectFail already carries
// what went wrong and where; without printing it a support log for exit 8 says
// only "rolled back" and never which file or why, which is the opposite of
// spec 11.3 ("persistent locks fail typed with the path") and H7 ("the --log
// file records which phase failed"). ASCII, never localized: this is a log.
QString effectTagName(int tag) {
    switch (tag) {
    case effecttag::probeDisk:
        return QStringLiteral("probe-disk");
    case effecttag::scanProcesses:
        return QStringLiteral("scan-processes");
    case effecttag::closeProcesses:
        return QStringLiteral("close-processes");
    case effecttag::relaunchElevated:
        return QStringLiteral("relaunch-elevated");
    case effecttag::writeJournalEntry:
        return QStringLiteral("write-journal-entry");
    case effecttag::ensureDir:
        return QStringLiteral("ensure-dir");
    case effecttag::copyVerify:
        return QStringLiteral("copy-verify");
    case effecttag::stageOld:
        return QStringLiteral("stage-old");
    case effecttag::promoteStaged:
        return QStringLiteral("promote-staged");
    case effecttag::deleteStale:
        return QStringLiteral("delete-stale");
    case effecttag::writeInstalledManifest:
        return QStringLiteral("write-installed-manifest");
    case effecttag::createShortcut:
        return QStringLiteral("create-shortcut");
    case effecttag::writeArp:
        return QStringLiteral("write-arp");
    case effecttag::commitCleanup:
        return QStringLiteral("commit-cleanup");
    case effecttag::rollbackStep:
        return QStringLiteral("rollback-step");
    case effecttag::launchApp:
        return QStringLiteral("launch-app");
    default:
        break;
    }
    return QStringLiteral("effect-%1").arg(tag);
}

QString setupErrorName(SetupError error) {
    switch (error) {
    case SetupError::None:
        return QStringLiteral("none");
    case SetupError::Internal:
        return QStringLiteral("internal");
    case SetupError::Usage:
        return QStringLiteral("usage");
    case SetupError::UnsupportedOs:
        return QStringLiteral("unsupported-os");
    case SetupError::NeedElevation:
        return QStringLiteral("need-elevation");
    case SetupError::AppRunning:
        return QStringLiteral("app-running");
    case SetupError::DiskFull:
        return QStringLiteral("disk-full");
    case SetupError::PayloadCorrupt:
        return QStringLiteral("payload-corrupt");
    case SetupError::FileOpFailed:
        return QStringLiteral("file-op-failed");
    case SetupError::RegistryFailed:
        return QStringLiteral("registry-failed");
    case SetupError::ShortcutFailed:
        return QStringLiteral("shortcut-failed");
    case SetupError::RollbackIncomplete:
        return QStringLiteral("rollback-incomplete");
    case SetupError::Cancelled:
        return QStringLiteral("cancelled");
    case SetupError::NothingInstalled:
        return QStringLiteral("nothing-installed");
    case SetupError::Downgrade:
        return QStringLiteral("downgrade");
    case SetupError::Busy:
        return QStringLiteral("busy");
    case SetupError::VersionMismatch:
        return QStringLiteral("version-mismatch");
    }
    return QStringLiteral("internal");
}

} // namespace

InstallCoordinator::InstallCoordinator(FileOps& fileOps, RegistryOps& registryOps,
                                       ShortcutOps& shortcutOps, ProcessOps& processOps,
                                       Logger& logger, QObject* parent)
    : QObject(parent), fileOps_(fileOps), registryOps_(registryOps), shortcutOps_(shortcutOps),
      processOps_(processOps), logger_(logger) {
    workerThread_.setObjectName(QStringLiteral("dish-setup-io"));
    workerContext_ = new QObject();
    workerContext_->moveToThread(&workerThread_);
    workerThread_.start();
}

InstallCoordinator::~InstallCoordinator() {
    runOnWorker([this] { journal_.close(); });
    workerThread_.quit();
    workerThread_.wait();
    delete workerContext_;
}

void InstallCoordinator::setStagingDir(const QString& dir) { stagingDir_ = dir; }
void InstallCoordinator::setSilent(bool silent) { silent_ = silent; }
void InstallCoordinator::setOldManifest(const std::optional<InstalledManifest>& manifest) {
    oldManifest_ = manifest;
}
void InstallCoordinator::setElevationRelaunch(const QString& exe, const QStringList& argv) {
    elevationExe_ = exe;
    elevationArgv_ = argv;
}

void InstallCoordinator::runOnWorker(std::function<void()> task) {
    QMetaObject::invokeMethod(workerContext_, std::move(task), Qt::QueuedConnection);
}

void InstallCoordinator::completeOnGui(std::function<void()> apply) {
    QMetaObject::invokeMethod(this, std::move(apply), Qt::QueuedConnection);
}

void InstallCoordinator::postEventFromWorker(const InstallEvent& event) {
    completeOnGui([this, event] {
        executing_ = false;
        dispatch(event);
        pump();
    });
}

void InstallCoordinator::start(InstallPlan plan, PayloadManifest manifest) {
    if (executing_ || (state_.phase != InstallPhase::Idle && state_.phase != InstallPhase::Failed &&
                       state_.phase != InstallPhase::Done)) {
        logger_.line(QStringLiteral("coordinator: start ignored, phase busy"));
        return;
    }
    pending_.clear();
    journalMirror_.clear();
    awaitingBlockers_ = false;
    closeAttempted_ = false;
    finishedEmitted_ = false;
    cancelFlag_ = false;
    currentRel_.clear();
    rollbackIndex_ = 0;
    manifest_ = manifest;
    runOnWorker([this] { journal_.close(); });
    logger_.line(QStringLiteral("install: begin \"%1\" version %2 (%3 files, %4 bytes)%5")
                     .arg(plan.installDir, manifest.version)
                     .arg(manifest.files.size())
                     .arg(manifest.totalBytes)
                     .arg(plan.isUpgrade ? QStringLiteral(" [upgrade]") : QString()));
    dispatch(event::Begin{std::move(plan), std::move(manifest)});
    pump();
}

void InstallCoordinator::requestCancel() {
    if (state_.phase == InstallPhase::Committing || state_.phase == InstallPhase::Finalizing ||
        state_.phase == InstallPhase::RollingBack) {
        return; // spec 3.5: cancel is ignored in these windows
    }
    dispatch(event::CancelRequested{});
    if (state_.phase == InstallPhase::Copying) {
        cancelFlag_ = true; // aborts the in-flight copy via its progress callback
    }
    pump();
}

void InstallCoordinator::resolveBlockers(bool force) {
    dispatch(event::CloseAppsRequested{force});
    pump();
}

void InstallCoordinator::requestElevatedRestart() {
    dispatch(event::ElevationRequested{});
    pump();
}

void InstallCoordinator::rescanBlockers() {
    if (executing_) { return; }
    executing_ = true;
    const QString dir = state_.plan.installDir;
    runOnWorker([this, dir] {
        const QVector<ProcInfo> procs = processOps_.processesUnder(dir);
        if (procs.isEmpty()) {
            postEventFromWorker(event::BlockersGone{});
        } else {
            postEventFromWorker(event::BlockersFound{procs});
        }
    });
}

void InstallCoordinator::recoverStaleJournalAt(const QString& installDir) {
    runOnWorker([this, installDir] {
        const bool clean = recoverStaleJournal(installDir, fileOps_, registryOps_, shortcutOps_);
        completeOnGui([this, clean, installDir] {
            logger_.line(QStringLiteral("recovery: stale journal at \"%1\" %2")
                             .arg(installDir, clean ? QStringLiteral("cleaned")
                                                    : QStringLiteral("left residue")));
            emit staleJournalRecovered(clean);
        });
    });
}

void InstallCoordinator::launchInstalledApp() {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, dir] {
        const QString exe = dir + QStringLiteral("/dish.exe");
        const OpResult r = processOps_.launchDetached(exe, {}, dir, true);
        if (!r.ok) {
            completeOnGui([this, exe] {
                logger_.line(QStringLiteral("launch: failed to start \"%1\"").arg(exe));
            });
        }
    });
}

void InstallCoordinator::dispatch(const InstallEvent& event) {
    const InstallPhase before = state_.phase;
    const int prevIndex = state_.fileIndex;
    const qint64 prevBytes = state_.bytesDone;

    Reduction reduction = reduce(state_, event);
    if (reduction.next) { state_ = *reduction.next; }

    if (const auto* found = std::get_if<event::BlockersFound>(&event)) {
        awaitingBlockers_ = true;
        emit blockers(found->procs);
    } else if (std::holds_alternative<event::BlockersGone>(event)) {
        awaitingBlockers_ = false;
        closeAttempted_ = false;
        emit blockers(QVector<ProcInfo>());
    } else if (std::holds_alternative<event::ElevationDeclined>(event)) {
        emit elevationDeclined();
    }

    // The only place the trigger of a rollback is still known: after this the
    // exit code reports the outcome (8 / 9), not the cause.
    if (const auto* failed = std::get_if<event::EffectFail>(&event)) {
        QString line = QStringLiteral("install: %1 failed (%2)")
                           .arg(effectTagName(failed->effectTag), setupErrorName(failed->error));
        if (!failed->path.isEmpty()) { line += QStringLiteral(" at \"%1\"").arg(failed->path); }
        logger_.line(line);
    }

    if (state_.phase != before) {
        if (state_.phase == InstallPhase::RollingBack) {
            awaitingBlockers_ = false; // a failed close gate must not stall the replay
            enterRollback();
        } else if (state_.phase == InstallPhase::Idle || state_.phase == InstallPhase::Done ||
                   state_.phase == InstallPhase::Failed) {
            awaitingBlockers_ = false;
            pending_.clear();
        }
    }
    for (auto it = reduction.effects.rbegin(); it != reduction.effects.rend(); ++it) {
        pending_.push_front(*it);
    }

    if (state_.phase != before) { emit phaseChanged(state_.phase); }
    if (state_.fileIndex != prevIndex || state_.bytesDone != prevBytes) {
        emit progress(state_.bytesDone, state_.bytesTotal, currentRel_);
    }

    // Silent policy: nobody clicks dialogs, so a blocked state resolves to
    // exit 5 — immediately under Abort, after one failed close otherwise.
    if (silent_ && awaitingBlockers_) {
        const bool abortPolicy = state_.plan.closePolicy == ClosePolicy::Abort;
        if (abortPolicy || closeAttempted_) {
            const int tag = abortPolicy ? effecttag::scanProcesses : effecttag::closeProcesses;
            completeOnGui([this, tag] {
                if (!awaitingBlockers_) { return; }
                awaitingBlockers_ = false;
                dispatch(event::EffectFail{tag, SetupError::AppRunning, state_.plan.installDir});
                pump();
            });
        }
    }
}

void InstallCoordinator::enterRollback() {
    pending_.clear();
    rollbackIndex_ = journalMirror_.size();
    logger_.line(QStringLiteral("rollback: replaying %1 journal entries in reverse")
                     .arg(journalMirror_.size()));
    runOnWorker([this] { journal_.close(); }); // undo may need to touch it
    for (int i = 0; i < journalMirror_.size(); ++i) {
        pending_.push_back(effect::RollbackStep{journalMirror_.at(i)});
    }
    // Reverse order: last journaled, first undone.
    std::reverse(pending_.begin(), pending_.end());
}

bool InstallCoordinator::frontMayRun(const InstallEffect& effect) const {
    if (std::holds_alternative<effect::Finish>(effect)) { return true; }
    if (state_.phase == InstallPhase::AwaitingBlockers ||
        state_.phase == InstallPhase::AwaitingElevation) {
        return isGateEffect(effect);
    }
    if (!isExecutingPhase(state_.phase)) { return false; }
    if (awaitingBlockers_) { return isGateEffect(effect); }
    return true;
}

void InstallCoordinator::pump() {
    if (executing_ || pending_.empty()) { return; }
    if (state_.phase == InstallPhase::Copying && state_.cancelRequested) {
        // Cancel arrived between effects: fail the pipeline where it stands;
        // the reducer rolls back (exit 10 once the replay completes).
        dispatch(event::EffectFail{effecttag::copyVerify, SetupError::Cancelled, QString()});
        pump();
        return;
    }
    if (!frontMayRun(pending_.front())) { return; }
    const InstallEffect effect = pending_.front();
    pending_.pop_front();
    execute(effect);
}

void InstallCoordinator::execute(const InstallEffect& effect) {
    executing_ = true;
    if (const auto* copy = std::get_if<effect::CopyVerify>(&effect)) {
        if (copy->manifestIndex >= 0 && copy->manifestIndex < manifest_.files.size()) {
            currentRel_ = manifest_.files.at(copy->manifestIndex).path;
        }
    }
    if (std::holds_alternative<effect::CloseProcesses>(effect)) { closeAttempted_ = true; }

    std::visit(
        [this](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, effect::ProbeDisk>) {
                execProbeDisk(e);
            } else if constexpr (std::is_same_v<T, effect::ScanProcesses>) {
                execScanProcesses(e);
            } else if constexpr (std::is_same_v<T, effect::CloseProcesses>) {
                execCloseProcesses(e);
            } else if constexpr (std::is_same_v<T, effect::RelaunchElevated>) {
                execRelaunchElevated(e);
            } else if constexpr (std::is_same_v<T, effect::WriteJournalEntry>) {
                execWriteJournalEntry(e);
            } else if constexpr (std::is_same_v<T, effect::EnsureDir>) {
                execEnsureDir(e);
            } else if constexpr (std::is_same_v<T, effect::CopyVerify>) {
                execCopyVerify(e);
            } else if constexpr (std::is_same_v<T, effect::StageOld>) {
                execStageOld(e);
            } else if constexpr (std::is_same_v<T, effect::PromoteStaged>) {
                execPromoteStaged(e);
            } else if constexpr (std::is_same_v<T, effect::DeleteStale>) {
                execDeleteStale();
            } else if constexpr (std::is_same_v<T, effect::WriteInstalledManifest>) {
                execWriteInstalledManifest();
            } else if constexpr (std::is_same_v<T, effect::CreateShortcut>) {
                execCreateShortcut(e);
            } else if constexpr (std::is_same_v<T, effect::WriteArp>) {
                execWriteArp(e);
            } else if constexpr (std::is_same_v<T, effect::CommitCleanup>) {
                execCommitCleanup();
            } else if constexpr (std::is_same_v<T, effect::RollbackStep>) {
                execRollbackStep(e, --rollbackIndex_);
            } else if constexpr (std::is_same_v<T, effect::LaunchApp>) {
                execLaunchApp(e);
            } else if constexpr (std::is_same_v<T, effect::Finish>) {
                execFinish(e);
            }
        },
        effect);
}

void InstallCoordinator::execProbeDisk(const effect::ProbeDisk& e) {
    runOnWorker([this, e] {
        const qint64 free = fileOps_.freeBytesFor(e.dir);
        if (free >= 0 && free < e.requiredBytes) {
            postEventFromWorker(event::PreflightFail{SetupError::DiskFull});
        } else {
            // An unknown answer (-1) does not block: the copy fails typed if
            // the volume is genuinely full.
            postEventFromWorker(event::PreflightOk{free});
        }
    });
}

void InstallCoordinator::execScanProcesses(const effect::ScanProcesses& e) {
    runOnWorker([this, e] {
        const QVector<ProcInfo> procs = processOps_.processesUnder(e.dir);
        if (procs.isEmpty()) {
            postEventFromWorker(event::BlockersGone{});
        } else {
            postEventFromWorker(event::BlockersFound{procs});
        }
    });
}

void InstallCoordinator::execCloseProcesses(const effect::CloseProcesses& e) {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, e, dir] {
        QVector<ProcInfo> procs = e.procs;
        if (procs.isEmpty()) { procs = processOps_.processesUnder(dir); }
        if (procs.isEmpty()) {
            postEventFromWorker(event::BlockersGone{});
            return;
        }
        processOps_.requestClose(procs, e.graceMs);
        if (e.force) {
            const QVector<ProcInfo> survivors = processOps_.processesUnder(dir);
            if (!survivors.isEmpty()) { processOps_.terminate(survivors); }
        }
        const QVector<ProcInfo> remaining = processOps_.processesUnder(dir);
        if (remaining.isEmpty()) {
            postEventFromWorker(event::BlockersGone{});
        } else if (e.force) {
            postEventFromWorker(event::EffectFail{effecttag::closeProcesses, SetupError::AppRunning,
                                                  remaining.first().imagePath});
        } else {
            postEventFromWorker(event::BlockersFound{remaining});
        }
    });
}

void InstallCoordinator::execRelaunchElevated(const effect::RelaunchElevated& e) {
    const QString exe = e.exe.isEmpty() ? elevationExe_ : e.exe;
    const QStringList argv = e.argv.isEmpty() ? elevationArgv_ : e.argv;
    runOnWorker([this, exe, argv] {
        const OpResult r = processOps_.relaunchElevated(exe, argv);
        if (r.ok) {
            postEventFromWorker(event::ElevationSpawned{});
        } else {
            completeOnGui([this, r] {
                logger_.line(QStringLiteral("elevation: relaunch failed (win32 %1)").arg(r.win32));
            });
            postEventFromWorker(event::ElevationDeclined{});
        }
    });
}

void InstallCoordinator::execWriteJournalEntry(const effect::WriteJournalEntry& e) {
    const QString installDir = state_.plan.installDir;
    JournalEntry entry = e.entry;
    // Enrichments the pure reducer cannot make: the resolved shortcut path
    // and, on upgrades, the previous ARP values a rollback must restore.
    if (entry.action == JournalAction::WroteArp && oldManifest_) {
        InstallPlan oldPlan;
        oldPlan.scope = scopeFromToken(oldManifest_->scope).value_or(state_.plan.scope);
        oldPlan.installDir = QDir::fromNativeSeparators(oldManifest_->installDir);
        qint64 oldTotal = 0;
        for (const PayloadEntry& file : oldManifest_->files) { oldTotal += file.size; }
        entry.prevArp = makeArpValues(oldPlan, oldManifest_->version, oldTotal);
    }
    runOnWorker([this, entry, installDir]() mutable {
        if (entry.action == JournalAction::CreatedShortcut && entry.path.isEmpty()) {
            const QStringList parts = entry.aux.split(QLatin1Char('|'));
            if (parts.size() == 2) {
                const auto location = shortcutLocationFromToken(parts.at(0));
                const auto scope = scopeFromToken(parts.at(1));
                if (location && scope) { entry.path = shortcutLinkPath(*location, *scope); }
            }
        }
        if (!journal_.isOpen()) {
            // The very first entry covers the install dir itself, which must
            // exist to hold the journal: created here, a step early, and
            // exactly what that entry records (see InstallMachine.h).
            const OpResult dir = fileOps_.ensureDir(installDir);
            if (!dir.ok || !journal_.open(journalFilePath(installDir))) {
                postEventFromWorker(event::EffectFail{effecttag::writeJournalEntry,
                                                      SetupError::FileOpFailed,
                                                      journalFilePath(installDir)});
                return;
            }
        }
        if (!journal_.append(entry)) {
            postEventFromWorker(event::EffectFail{effecttag::writeJournalEntry,
                                                  SetupError::FileOpFailed, journal_.path()});
            return;
        }
        completeOnGui([this, entry] {
            journalMirror_.append(entry);
            executing_ = false;
            dispatch(event::EffectOk{effecttag::writeJournalEntry, 0});
            pump();
        });
    });
}

void InstallCoordinator::execEnsureDir(const effect::EnsureDir& e) {
    runOnWorker([this, e] {
        const OpResult r = fileOps_.ensureDir(e.abs);
        if (r.ok) {
            postEventFromWorker(event::EffectOk{effecttag::ensureDir, 0});
        } else {
            postEventFromWorker(event::EffectFail{effecttag::ensureDir, r.error, r.path});
        }
    });
}

void InstallCoordinator::execCopyVerify(const effect::CopyVerify& e) {
    QString from = e.fromAbs;
    if (!QDir::isAbsolutePath(from) && !stagingDir_.isEmpty()) {
        from = stagingDir_ + QLatin1Char('/') + from;
    }
    QByteArray expected;
    if (e.manifestIndex >= 0 && e.manifestIndex < manifest_.files.size()) {
        expected = manifest_.files.at(e.manifestIndex).sha256Hex;
    }
    runOnWorker([this, e, from, expected] {
        qint64 sinceLast = 0;
        const auto postProgress = [this](qint64 delta) {
            completeOnGui([this, delta] { dispatch(event::CopyProgress{delta}); });
        };
        const OpResult copy = fileOps_.copyWithProgress(from, e.toAbs, [&](qint64 delta) -> bool {
            sinceLast += delta;
            if (sinceLast >= 256 * 1024) {
                postProgress(sinceLast);
                sinceLast = 0;
            }
            return !cancelFlag_.load();
        });
        if (sinceLast > 0) { postProgress(sinceLast); }
        if (!copy.ok) {
            postEventFromWorker(event::EffectFail{effecttag::copyVerify, copy.error, copy.path});
            return;
        }
        const OpResult verify = fileOps_.verifySha256(e.toAbs, expected);
        if (!verify.ok) {
            postEventFromWorker(
                event::EffectFail{effecttag::copyVerify, verify.error, verify.path});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::copyVerify, e.manifestIndex});
    });
}

void InstallCoordinator::execStageOld(const effect::StageOld& e) {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, e, dir] {
        const QString src = dir + QLatin1Char('/') + e.relPath;
        if (!fileOps_.exists(src)) {
            // Nothing to displace (new file, or a repair over a partial
            // install): the journaled entry's undo is a no-op by design.
            postEventFromWorker(event::EffectOk{effecttag::stageOld, 0});
            return;
        }
        const QString backup = dir + QLatin1Char('/') + oldDirName() + QLatin1Char('/') + e.relPath;
        const OpResult parent = fileOps_.ensureDir(parentOf(backup));
        if (!parent.ok) {
            postEventFromWorker(event::EffectFail{effecttag::stageOld, parent.error, parent.path});
            return;
        }
        const OpResult r = fileOps_.rename(src, backup);
        if (!r.ok) {
            postEventFromWorker(event::EffectFail{effecttag::stageOld, r.error, src});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::stageOld, 0});
    });
}

void InstallCoordinator::execPromoteStaged(const effect::PromoteStaged& e) {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, e, dir] {
        const QString staged =
            dir + QLatin1Char('/') + stageDirName() + QLatin1Char('/') + e.relPath;
        const QString final_ = dir + QLatin1Char('/') + e.relPath;
        // A brand-new subdirectory exists only under .dish-stage so far; the
        // final parent is created unjournaled (an empty leftover dir after a
        // rollback is cosmetic and the uninstaller prunes empties anyway).
        const OpResult parent = fileOps_.ensureDir(parentOf(final_));
        if (!parent.ok) {
            postEventFromWorker(
                event::EffectFail{effecttag::promoteStaged, parent.error, parent.path});
            return;
        }
        const OpResult r = fileOps_.rename(staged, final_);
        if (!r.ok) {
            postEventFromWorker(event::EffectFail{effecttag::promoteStaged, r.error, staged});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::promoteStaged, 0});
    });
}

void InstallCoordinator::execDeleteStale() {
    const QString dir = state_.plan.installDir;
    QSet<QString> keep;
    for (const PayloadEntry& entry : manifest_.files) { keep.insert(entry.path.toCaseFolded()); }
    QStringList stale;
    if (oldManifest_) {
        for (const PayloadEntry& entry : oldManifest_->files) {
            if (!keep.contains(entry.path.toCaseFolded())) { stale.append(entry.path); }
        }
    }
    runOnWorker([this, dir, stale] {
        QVector<JournalEntry> appended;
        const auto finish = [this, &appended](const std::optional<event::EffectFail>& fail) {
            const QVector<JournalEntry> persisted = appended;
            completeOnGui([this, persisted, fail] {
                journalMirror_.append(persisted);
                executing_ = false;
                if (fail) {
                    dispatch(*fail);
                } else {
                    dispatch(event::EffectOk{effecttag::deleteStale, 0});
                }
                pump();
            });
        };
        for (const QString& rel : stale) {
            const QString src = dir + QLatin1Char('/') + rel;
            if (!fileOps_.exists(src)) { continue; }
            const QString backup = dir + QLatin1Char('/') + oldDirName() + QLatin1Char('/') + rel;
            JournalEntry entry;
            entry.action = JournalAction::StagedOld;
            entry.path = src;
            entry.aux = backup;
            if (!journal_.append(entry)) {
                finish(event::EffectFail{effecttag::deleteStale, SetupError::FileOpFailed,
                                         journal_.path()});
                return;
            }
            appended.append(entry);
            const OpResult parent = fileOps_.ensureDir(parentOf(backup));
            if (!parent.ok) {
                finish(event::EffectFail{effecttag::deleteStale, parent.error, parent.path});
                return;
            }
            const OpResult r = fileOps_.rename(src, backup);
            if (!r.ok) {
                finish(event::EffectFail{effecttag::deleteStale, r.error, src});
                return;
            }
        }
        finish(std::nullopt);
    });
}

void InstallCoordinator::execWriteInstalledManifest() {
    const InstallPlan plan = state_.plan;
    const PayloadManifest manifest = manifest_;
    runOnWorker([this, plan, manifest] {
        InstalledManifest installed;
        installed.schema = 1;
        installed.version = manifest.version;
        installed.installDir = QDir::toNativeSeparators(QDir::cleanPath(plan.installDir));
        installed.scope = scopeToken(plan.scope);
        installed.startMenu = plan.startMenu;
        installed.desktop = plan.desktop;
        // Deterministic: identical to what CreateShortcut resolves right
        // after this write (the manifest precedes the shortcuts so ARP can
        // still be last).
        if (plan.startMenu) {
            const QString link = shortcutLinkPath(ShortcutLocation::StartMenu, plan.scope);
            if (!link.isEmpty()) { installed.shortcutPaths.append(link); }
        }
        if (plan.desktop) {
            const QString link = shortcutLinkPath(ShortcutLocation::Desktop, plan.scope);
            if (!link.isEmpty()) { installed.shortcutPaths.append(link); }
        }
        installed.installedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        installed.files = manifest.files;
        for (PayloadEntry& entry : installed.files) { entry.stagedAs = entry.path; }

        const QString path = plan.installDir + QLatin1Char('/') + installedManifestFileName();
        QSaveFile save(path);
        if (!save.open(QIODevice::WriteOnly)) {
            postEventFromWorker(event::EffectFail{effecttag::writeInstalledManifest,
                                                  SetupError::FileOpFailed, path});
            return;
        }
        const QByteArray json = installed.toJson();
        if (save.write(json) != json.size() || !save.commit()) {
            postEventFromWorker(event::EffectFail{effecttag::writeInstalledManifest,
                                                  SetupError::FileOpFailed, path});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::writeInstalledManifest, 0});
    });
}

void InstallCoordinator::execCreateShortcut(const effect::CreateShortcut& e) {
    runOnWorker([this, e] {
        const OpResult r = shortcutOps_.create(e.spec);
        if (!r.ok) {
            postEventFromWorker(event::EffectFail{effecttag::createShortcut, r.error, r.path});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::createShortcut, 0});
    });
}

void InstallCoordinator::execWriteArp(const effect::WriteArp& e) {
    const Scope scope = state_.plan.scope;
    runOnWorker([this, e, scope] {
        const OpResult r = registryOps_.writeArp(scope, e.values);
        if (!r.ok) {
            postEventFromWorker(event::EffectFail{effecttag::writeArp, r.error, r.path});
            return;
        }
        postEventFromWorker(event::EffectOk{effecttag::writeArp, 0});
    });
}

void InstallCoordinator::execCommitCleanup() {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, dir] {
        journal_.close();
        // Best-effort by design: a locked backup never fails a completed
        // install; the next run's stale sweep collects leftovers.
        removeTreeBestEffort(fileOps_, dir + QLatin1Char('/') + oldDirName());
        removeTreeBestEffort(fileOps_, dir + QLatin1Char('/') + stageDirName());
        fileOps_.remove(journalFilePath(dir));
        postEventFromWorker(event::EffectOk{effecttag::commitCleanup, 0});
    });
}

void InstallCoordinator::execRollbackStep(const effect::RollbackStep& e, int index) {
    runOnWorker([this, e, index] {
        const bool ok = applyJournalUndo(e.entry, fileOps_, registryOps_, shortcutOps_);
        if (!ok) {
            completeOnGui([this, e] {
                logger_.line(QStringLiteral("rollback: undo failed for \"%1\"").arg(e.entry.path));
            });
        }
        postEventFromWorker(event::RollbackStepDone{index, ok});
    });
}

void InstallCoordinator::execLaunchApp(const effect::LaunchApp& e) {
    const QString dir = state_.plan.installDir;
    runOnWorker([this, e, dir] {
        const QString exe = dir + QStringLiteral("/dish.exe");
        const OpResult r = processOps_.launchDetached(exe, {}, dir, e.deElevate);
        if (!r.ok) {
            completeOnGui([this, exe] {
                logger_.line(QStringLiteral("launch: failed to start \"%1\"").arg(exe));
            });
        }
        postEventFromWorker(event::EffectOk{effecttag::launchApp, 0});
    });
}

void InstallCoordinator::execFinish(const effect::Finish& e) {
    const QString dir = state_.plan.installDir;
    // Only a CLEAN rollback removes its journal: an incomplete one leaves it
    // (plus .dish-old) for the next run's recovery offer (spec 11.2).
    const bool cleanRollback = state_.phase == InstallPhase::Failed &&
                               state_.error != SetupError::RollbackIncomplete &&
                               !journalMirror_.isEmpty();
    runOnWorker([this, e, dir, cleanRollback] {
        journal_.close();
        if (cleanRollback) {
            fileOps_.remove(journalFilePath(dir));
            removeTreeBestEffort(fileOps_, dir + QLatin1Char('/') + oldDirName());
            removeTreeBestEffort(fileOps_, dir + QLatin1Char('/') + stageDirName());
            fileOps_.removeDirIfEmpty(dir);
        }
        completeOnGui([this, e] {
            executing_ = false;
            dispatch(event::EffectOk{effecttag::finish, 0});
            if (!finishedEmitted_) {
                finishedEmitted_ = true;
                logger_.line(QStringLiteral("install: finished, exit %1 (%2)")
                                 .arg(static_cast<int>(e.code))
                                 .arg(QLatin1String(resultToken(e.code))));
                emit finished(e.code, state_.error, state_.errorPath);
            }
            pump();
        });
    });
}

} // namespace dish::installer
