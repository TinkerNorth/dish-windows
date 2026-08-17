// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Drives the pure InstallMachine (spec 3.8): owns InstallState on the GUI
// thread, runs reduce() there (microseconds), and executes every effect that
// touches disk/registry/COM/process APIs on ONE worker QThread
// ("dish-setup-io") via queued lambdas — results come back queued, so state
// mutation is single-threaded by construction.
//
// Queue discipline (see InstallMachine.h's execution model): the pipeline
// arrives in full at Begin; effects run strictly in order, one in flight.
// Gating phases pause the pipeline but still execute gate-resolution effects
// (ScanProcesses / CloseProcesses / RelaunchElevated). Entering RollingBack
// drops the pipeline and replays the coordinator's journal mirror in reverse
// as RollbackStep executions. Entering Idle/Done/Failed drops everything.

#pragma once

#include "installer/CliOptions.h"
#include "installer/InstallMachine.h"
#include "installer/Journal.h"
#include "installer/Logger.h"
#include "installer/Manifest.h"
#include "installer/ops/FileOps.h"
#include "installer/ops/ProcessOps.h"
#include "installer/ops/RegistryOps.h"
#include "installer/ops/ShortcutOps.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <atomic>
#include <deque>
#include <optional>

namespace dish::installer {

class InstallCoordinator : public QObject {
    Q_OBJECT

  public:
    // The four ops + Logger are injected and must outlive the coordinator
    // (spec 3.8). Fakes slot in for tests.
    InstallCoordinator(FileOps& fileOps, RegistryOps& registryOps, ShortcutOps& shortcutOps,
                       ProcessOps& processOps, Logger& logger, QObject* parent = nullptr);
    ~InstallCoordinator() override;

    // Context the reducer deliberately never learns; set before start().
    void setStagingDir(const QString& dir); // extracted-image root for CopyVerify
    void setSilent(bool silent);            // silent policy: blockers auto-fail
    void setOldManifest(const std::optional<InstalledManifest>& manifest); // upgrade context
    void setElevationRelaunch(const QString& exe, const QStringList& argv);

    const InstallState& state() const { return state_; }
    int fileCount() const { return manifest_.files.size(); }

  public slots:
    void start(dish::installer::InstallPlan plan, dish::installer::PayloadManifest manifest);
    void requestCancel(); // no-op during Committing/Finalizing/RollingBack
    void resolveBlockers(bool force);
    void requestElevatedRestart();
    void rescanBlockers();
    // The crashed-previous-attempt sweep, run on the worker;
    // staleJournalRecovered fires when done. Silent flows instead call
    // dish::installer::recoverStaleJournal inline.
    void recoverStaleJournalAt(const QString& installDir);
    void launchInstalledApp(); // Done page "Start Dish now" (de-elevated)

  signals:
    void phaseChanged(dish::installer::InstallPhase phase);
    void progress(qint64 done, qint64 total, QString rel);
    void blockers(QVector<dish::installer::ProcInfo> procs);
    void elevationDeclined();
    void finished(dish::installer::ExitCode code, dish::installer::SetupError error, QString path);
    void staleJournalRecovered(bool clean);

  private:
    void dispatch(const InstallEvent& event);
    void pump();
    void execute(const InstallEffect& effect);
    bool frontMayRun(const InstallEffect& effect) const;
    void enterRollback();
    void postEventFromWorker(const InstallEvent& event);
    void runOnWorker(std::function<void()> task);
    void completeOnGui(std::function<void()> apply);

    // effect executors (worker thread)
    void execProbeDisk(const effect::ProbeDisk& e);
    void execScanProcesses(const effect::ScanProcesses& e);
    void execCloseProcesses(const effect::CloseProcesses& e);
    void execRelaunchElevated(const effect::RelaunchElevated& e);
    void execWriteJournalEntry(const effect::WriteJournalEntry& e);
    void execEnsureDir(const effect::EnsureDir& e);
    void execCopyVerify(const effect::CopyVerify& e);
    void execStageOld(const effect::StageOld& e);
    void execPromoteStaged(const effect::PromoteStaged& e);
    void execDeleteStale();
    void execWriteInstalledManifest();
    void execCreateShortcut(const effect::CreateShortcut& e);
    void execWriteArp(const effect::WriteArp& e);
    void execCommitCleanup();
    void execRollbackStep(const effect::RollbackStep& e, int index);
    void execLaunchApp(const effect::LaunchApp& e);
    void execFinish(const effect::Finish& e);

    FileOps& fileOps_;
    RegistryOps& registryOps_;
    ShortcutOps& shortcutOps_;
    ProcessOps& processOps_;
    Logger& logger_;

    QThread workerThread_;
    QObject* workerContext_ = nullptr; // lives on workerThread_; lambda anchor

    InstallState state_;
    PayloadManifest manifest_;
    std::optional<InstalledManifest> oldManifest_;
    QString stagingDir_;
    QString elevationExe_;
    QStringList elevationArgv_;
    bool silent_ = false;

    std::deque<InstallEffect> pending_;
    bool executing_ = false;
    bool awaitingBlockers_ = false;
    bool closeAttempted_ = false;
    bool finishedEmitted_ = false;
    int rollbackIndex_ = 0;
    QString currentRel_;
    QVector<JournalEntry> journalMirror_; // GUI-side copy for the rollback replay
    JournalWriter journal_;               // worker-side only after open
    std::atomic<bool> cancelFlag_{false};
};

} // namespace dish::installer
