// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Drives the pure UninstallMachine, mirroring InstallCoordinator (spec 3.8):
// state and reduce() on the GUI thread, every effect on the "dish-setup-io"
// worker, strict sequential queue. Uninstall is forward-only: undeletable
// files (working set, sharing violations) are collected into the leftover
// list the one-hop helper finishes after this process exits (spec 11.1). The
// helper deletes the ARP key LAST, so a blocked cleanup leaves a retryable
// entry.

#pragma once

#include "installer/CliOptions.h"
#include "installer/Logger.h"
#include "installer/Manifest.h"
#include "installer/UninstallMachine.h"
#include "installer/ops/FileOps.h"
#include "installer/ops/ProcessOps.h"
#include "installer/ops/RegistryOps.h"
#include "installer/ops/ShortcutOps.h"

#include <QObject>
#include <QStringList>
#include <QThread>
#include <QVector>

#include <deque>
#include <functional>

namespace dish::installer {

class UninstallCoordinator : public QObject {
    Q_OBJECT

  public:
    UninstallCoordinator(FileOps& fileOps, RegistryOps& registryOps, ShortcutOps& shortcutOps,
                         ProcessOps& processOps, Logger& logger, QObject* parent = nullptr);
    ~UninstallCoordinator() override;

    void setSilent(bool silent);
    void setClosePolicy(ClosePolicy policy);
    // The uninstaller's own mapped-module probe (Win32ProcessOps::
    // ownWorkingSetUnder in production; a stub in tests). Files it returns are
    // deferred to the helper instead of deleted in-process.
    void setWorkingSetProbe(std::function<QStringList(const QString&)> probe);
    // Purge's registry half (Win32RegistryOps::purgeUserSettingsTrees in
    // production); injectable so tests never touch the real HKCU.
    void setPurgeRegistryHook(std::function<void()> hook);

    const UninstallState& state() const { return state_; }

  public slots:
    void start(dish::installer::InstalledManifest manifest);
    void setPurgeUserData(bool purge);
    void requestCancel();
    void resolveBlockers(bool force);
    void rescanBlockers();

  signals:
    void phaseChanged(dish::installer::UninstallPhase phase);
    void progress(qint64 done, qint64 total, QString rel);
    void blockers(QVector<dish::installer::ProcInfo> procs);
    void finished(dish::installer::ExitCode code, dish::installer::SetupError error, QString path);

  private:
    void dispatch(const UninstallEvent& event);
    void pump();
    void execute(const UninstallEffect& effect);
    bool frontMayRun(const UninstallEffect& effect) const;
    void runOnWorker(std::function<void()> task);
    void completeOnGui(std::function<void()> apply);
    void postEventFromWorker(const UninstallEvent& event);

    void execScanProcesses(const ueffect::ScanProcesses& e);
    void execCloseProcesses(const ueffect::CloseProcesses& e);
    void execRemoveShortcut(const ueffect::RemoveShortcut& e);
    void execRemoveFile(const ueffect::RemoveFile& e, int index);
    void execRemoveResidue(const ueffect::RemoveResidue& e);
    void execRemoveUpdatesCache();
    void execPurgeUserData();
    void execPruneDirs(const ueffect::PruneDirs& e);
    void execSpawnHelper();
    void execFinish(const ueffect::Finish& e);

    FileOps& fileOps_;
    RegistryOps& registryOps_;
    ShortcutOps& shortcutOps_;
    ProcessOps& processOps_;
    Logger& logger_;

    QThread workerThread_;
    QObject* workerContext_ = nullptr;

    UninstallState state_;
    InstalledManifest manifest_;
    bool purge_ = false;
    bool silent_ = false;
    ClosePolicy closePolicy_ = ClosePolicy::Abort;
    std::function<QStringList(const QString&)> workingSetProbe_;
    std::function<void()> purgeRegistryHook_;

    std::deque<UninstallEffect> pending_;
    bool executing_ = false;
    bool awaitingBlockers_ = false;
    bool closeAttempted_ = false;
    bool finishedEmitted_ = false;
    int removeIndex_ = 0;
    QString currentRel_;
    QStringList leftovers_;  // worker-side after start
    QStringList workingSet_; // worker-side after first RemoveFile
    bool workingSetProbed_ = false;
};

} // namespace dish::installer
