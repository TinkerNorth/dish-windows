// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/UninstallCoordinator.h"

#include "installer/InstallPlan.h"
#include "installer/Journal.h"
#include "installer/ops/KnownFolders.h"
#include "installer/ops/Win32FileOps.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRandomGenerator>

#include <algorithm>

namespace dish::installer {

namespace {

bool isGateEffect(const UninstallEffect& effect) {
    return std::holds_alternative<ueffect::ScanProcesses>(effect) ||
           std::holds_alternative<ueffect::CloseProcesses>(effect);
}

// UTF-16LE with BOM, CRLF, one absolute native path per line: trivially
// consumed by the Qt-free wide-API helper.
bool writeUtf16List(const QString& path, const QStringList& lines) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    QString text = QString(QChar(0xFEFF)); // BOM
    for (const QString& line : lines) {
        text += QDir::toNativeSeparators(line);
        text += QStringLiteral("\r\n");
    }
    const QByteArray bytes(reinterpret_cast<const char*>(text.utf16()),
                           text.size() * static_cast<qsizetype>(sizeof(char16_t)));
    const bool ok = file.write(bytes) == bytes.size();
    file.close();
    return ok;
}

} // namespace

UninstallCoordinator::UninstallCoordinator(FileOps& fileOps, RegistryOps& registryOps,
                                           ShortcutOps& shortcutOps, ProcessOps& processOps,
                                           Logger& logger, QObject* parent)
    : QObject(parent), fileOps_(fileOps), registryOps_(registryOps), shortcutOps_(shortcutOps),
      processOps_(processOps), logger_(logger) {
    workerThread_.setObjectName(QStringLiteral("dish-setup-io"));
    workerContext_ = new QObject();
    workerContext_->moveToThread(&workerThread_);
    workerThread_.start();
}

UninstallCoordinator::~UninstallCoordinator() {
    workerThread_.quit();
    workerThread_.wait();
    delete workerContext_;
}

void UninstallCoordinator::setSilent(bool silent) { silent_ = silent; }
void UninstallCoordinator::setClosePolicy(ClosePolicy policy) { closePolicy_ = policy; }
void UninstallCoordinator::setWorkingSetProbe(std::function<QStringList(const QString&)> probe) {
    workingSetProbe_ = std::move(probe);
}
void UninstallCoordinator::setPurgeRegistryHook(std::function<void()> hook) {
    purgeRegistryHook_ = std::move(hook);
}

void UninstallCoordinator::setPurgeUserData(bool purge) { purge_ = purge; }

void UninstallCoordinator::runOnWorker(std::function<void()> task) {
    QMetaObject::invokeMethod(workerContext_, std::move(task), Qt::QueuedConnection);
}

void UninstallCoordinator::completeOnGui(std::function<void()> apply) {
    QMetaObject::invokeMethod(this, std::move(apply), Qt::QueuedConnection);
}

void UninstallCoordinator::postEventFromWorker(const UninstallEvent& event) {
    completeOnGui([this, event] {
        executing_ = false;
        dispatch(event);
        pump();
    });
}

void UninstallCoordinator::start(InstalledManifest manifest) {
    if (executing_ || state_.phase != UninstallPhase::Idle) {
        logger_.line(QStringLiteral("uninstall: start ignored, phase busy"));
        return;
    }
    manifest_ = manifest;
    manifest_.installDir = QDir::fromNativeSeparators(manifest_.installDir);
    pending_.clear();
    leftovers_.clear();
    workingSet_.clear();
    workingSetProbed_ = false;
    awaitingBlockers_ = false;
    closeAttempted_ = false;
    finishedEmitted_ = false;
    removeIndex_ = 0;
    logger_.line(QStringLiteral("uninstall: begin \"%1\" version %2 (%3 files)%4")
                     .arg(manifest_.installDir, manifest_.version)
                     .arg(manifest_.files.size())
                     .arg(purge_ ? QStringLiteral(" [purge]") : QString()));
    InstalledManifest forMachine = manifest_;
    dispatch(uevent::Begin{std::move(forMachine), purge_});
    pump();
}

void UninstallCoordinator::requestCancel() {
    dispatch(uevent::CancelRequested{});
    pump();
}

void UninstallCoordinator::resolveBlockers(bool force) {
    dispatch(uevent::CloseAppsRequested{force});
    pump();
}

void UninstallCoordinator::rescanBlockers() {
    if (executing_) { return; }
    executing_ = true;
    const QString dir = state_.installDir;
    runOnWorker([this, dir] {
        const QVector<ProcInfo> procs = processOps_.processesUnder(dir);
        if (procs.isEmpty()) {
            postEventFromWorker(uevent::BlockersGone{});
        } else {
            postEventFromWorker(uevent::BlockersFound{procs});
        }
    });
}

void UninstallCoordinator::dispatch(const UninstallEvent& event) {
    const UninstallPhase before = state_.phase;
    const int prevIndex = state_.fileIndex;

    UninstallReduction reduction = reduce(state_, event);
    if (reduction.next) { state_ = *reduction.next; }

    if (const auto* found = std::get_if<uevent::BlockersFound>(&event)) {
        awaitingBlockers_ = true;
        emit blockers(found->procs);
    } else if (std::holds_alternative<uevent::BlockersGone>(event)) {
        awaitingBlockers_ = false;
        closeAttempted_ = false;
        emit blockers(QVector<ProcInfo>());
    }

    if (state_.phase != before &&
        (state_.phase == UninstallPhase::Done || state_.phase == UninstallPhase::Failed)) {
        awaitingBlockers_ = false;
        pending_.clear();
    }
    for (auto it = reduction.effects.rbegin(); it != reduction.effects.rend(); ++it) {
        pending_.push_front(*it);
    }

    if (state_.phase != before) { emit phaseChanged(state_.phase); }
    if (state_.fileIndex != prevIndex) {
        emit progress(state_.fileIndex, state_.fileCount, currentRel_);
    }

    // Silent policy mirrors the install side: Abort fails immediately, one
    // graceful attempt otherwise. The machine carries no policy, so the
    // coordinator injects the close request itself.
    if (silent_ && awaitingBlockers_) {
        if (closePolicy_ == ClosePolicy::Abort || closeAttempted_) {
            const int tag = closePolicy_ == ClosePolicy::Abort ? effecttag::scanProcesses
                                                               : effecttag::closeProcesses;
            completeOnGui([this, tag] {
                if (!awaitingBlockers_) { return; }
                awaitingBlockers_ = false;
                dispatch(uevent::EffectFail{tag, SetupError::AppRunning, state_.installDir});
                pump();
            });
        } else {
            completeOnGui([this] {
                if (!awaitingBlockers_) { return; }
                dispatch(uevent::CloseAppsRequested{closePolicy_ == ClosePolicy::Force});
                pump();
            });
        }
    }
}

bool UninstallCoordinator::frontMayRun(const UninstallEffect& effect) const {
    if (std::holds_alternative<ueffect::Finish>(effect)) { return true; }
    if (state_.phase == UninstallPhase::AwaitingBlockers || awaitingBlockers_) {
        return isGateEffect(effect);
    }
    return state_.phase == UninstallPhase::Preflight ||
           state_.phase == UninstallPhase::RemovingShortcuts ||
           state_.phase == UninstallPhase::RemovingFiles ||
           state_.phase == UninstallPhase::PurgingData ||
           state_.phase == UninstallPhase::HandingOff;
}

void UninstallCoordinator::pump() {
    if (executing_ || pending_.empty()) { return; }
    if (!frontMayRun(pending_.front())) { return; }
    const UninstallEffect effect = pending_.front();
    pending_.pop_front();
    execute(effect);
}

void UninstallCoordinator::execute(const UninstallEffect& effect) {
    executing_ = true;
    if (std::holds_alternative<ueffect::CloseProcesses>(effect)) { closeAttempted_ = true; }
    if (const auto* removeFile = std::get_if<ueffect::RemoveFile>(&effect)) {
        currentRel_ = QDir(state_.installDir).relativeFilePath(removeFile->abs);
    }

    std::visit(
        [this](const auto& e) {
            using T = std::decay_t<decltype(e)>;
            if constexpr (std::is_same_v<T, ueffect::ScanProcesses>) {
                execScanProcesses(e);
            } else if constexpr (std::is_same_v<T, ueffect::CloseProcesses>) {
                execCloseProcesses(e);
            } else if constexpr (std::is_same_v<T, ueffect::RemoveShortcut>) {
                execRemoveShortcut(e);
            } else if constexpr (std::is_same_v<T, ueffect::RemoveFile>) {
                execRemoveFile(e, removeIndex_++);
            } else if constexpr (std::is_same_v<T, ueffect::RemoveResidue>) {
                execRemoveResidue(e);
            } else if constexpr (std::is_same_v<T, ueffect::RemoveUpdatesCache>) {
                execRemoveUpdatesCache();
            } else if constexpr (std::is_same_v<T, ueffect::PurgeUserData>) {
                execPurgeUserData();
            } else if constexpr (std::is_same_v<T, ueffect::PruneDirs>) {
                execPruneDirs(e);
            } else if constexpr (std::is_same_v<T, ueffect::SpawnHelper>) {
                execSpawnHelper();
            } else if constexpr (std::is_same_v<T, ueffect::Finish>) {
                execFinish(e);
            }
        },
        effect);
}

void UninstallCoordinator::execScanProcesses(const ueffect::ScanProcesses& e) {
    runOnWorker([this, e] {
        const QVector<ProcInfo> procs = processOps_.processesUnder(e.dir);
        if (procs.isEmpty()) {
            postEventFromWorker(uevent::BlockersGone{});
        } else {
            postEventFromWorker(uevent::BlockersFound{procs});
        }
    });
}

void UninstallCoordinator::execCloseProcesses(const ueffect::CloseProcesses& e) {
    const QString dir = state_.installDir;
    runOnWorker([this, e, dir] {
        QVector<ProcInfo> procs = e.procs;
        if (procs.isEmpty()) { procs = processOps_.processesUnder(dir); }
        if (procs.isEmpty()) {
            postEventFromWorker(uevent::BlockersGone{});
            return;
        }
        processOps_.requestClose(procs, e.graceMs > 0 ? e.graceMs : 10000);
        if (e.force) {
            const QVector<ProcInfo> survivors = processOps_.processesUnder(dir);
            if (!survivors.isEmpty()) { processOps_.terminate(survivors); }
        }
        const QVector<ProcInfo> remaining = processOps_.processesUnder(dir);
        if (remaining.isEmpty()) {
            postEventFromWorker(uevent::BlockersGone{});
        } else if (e.force) {
            postEventFromWorker(uevent::EffectFail{
                effecttag::closeProcesses, SetupError::AppRunning, remaining.first().imagePath});
        } else {
            postEventFromWorker(uevent::BlockersFound{remaining});
        }
    });
}

void UninstallCoordinator::execRemoveShortcut(const ueffect::RemoveShortcut& e) {
    runOnWorker([this, e] {
        const OpResult r = shortcutOps_.remove(e.linkAbs);
        if (!r.ok) {
            // Best-effort: the helper retries it with backoff after exit.
            leftovers_.append(e.linkAbs);
            completeOnGui([this, e] {
                logger_.line(
                    QStringLiteral("uninstall: shortcut deferred to helper \"%1\"").arg(e.linkAbs));
            });
        }
        postEventFromWorker(uevent::EffectOk{effecttag::removeShortcut, 0});
    });
}

void UninstallCoordinator::execRemoveFile(const ueffect::RemoveFile& e, int index) {
    const QString dir = state_.installDir;
    runOnWorker([this, e, index, dir] {
        if (!workingSetProbed_) {
            workingSetProbed_ = true;
            // The files this process provably cannot delete: its own image,
            // the helper it will spawn, and every DLL it has mapped from the
            // install dir (spec 3.6).
            workingSet_.append(dir + QStringLiteral("/uninstall.exe"));
            workingSet_.append(dir + QStringLiteral("/uninstall-helper.exe"));
            workingSet_.append(QDir::fromNativeSeparators(QCoreApplication::applicationFilePath()));
            if (workingSetProbe_) { workingSet_ += workingSetProbe_(dir); }
            for (QString& path : workingSet_) { path = QDir::cleanPath(path).toCaseFolded(); }
        }
        const QString folded = QDir::cleanPath(e.abs).toCaseFolded();
        if (workingSet_.contains(folded)) {
            leftovers_.append(e.abs);
            postEventFromWorker(uevent::EffectOk{effecttag::removeFile, index});
            return;
        }
        const OpResult r = fileOps_.remove(e.abs);
        if (!r.ok) {
            // Sharing violations and friends join the helper list instead of
            // failing the uninstall (spec 3.6).
            leftovers_.append(e.abs);
            completeOnGui([this, e] {
                logger_.line(
                    QStringLiteral("uninstall: file deferred to helper \"%1\"").arg(e.abs));
            });
        }
        postEventFromWorker(uevent::EffectOk{effecttag::removeFile, index});
    });
}

void UninstallCoordinator::execRemoveResidue(const ueffect::RemoveResidue& e) {
    runOnWorker([this, e] {
        fileOps_.remove(e.dir + QLatin1Char('/') + installedManifestFileName());
        fileOps_.remove(journalFilePath(e.dir));
        removeTreeBestEffort(fileOps_, e.dir + QLatin1Char('/') + oldDirName());
        removeTreeBestEffort(fileOps_, e.dir + QLatin1Char('/') + stageDirName());
        postEventFromWorker(uevent::EffectOk{effecttag::removeResidue, 0});
    });
}

void UninstallCoordinator::execRemoveUpdatesCache() {
    runOnWorker([this] {
        // Unconditional (spec D13): the updater cache is not user data.
        const QString cache = updatesCacheDir();
        if (!cache.isEmpty()) { removeTreeBestEffort(fileOps_, cache); }
        postEventFromWorker(uevent::EffectOk{effecttag::removeUpdatesCache, 0});
    });
}

void UninstallCoordinator::execPurgeUserData() {
    runOnWorker([this] {
        if (purgeRegistryHook_) { purgeRegistryHook_(); }
        const QString localAppData = localAppDataDir();
        if (!localAppData.isEmpty()) {
            removeTreeBestEffort(fileOps_, localAppData + QStringLiteral("/Dish"));
        }
        postEventFromWorker(uevent::EffectOk{effecttag::purgeUserData, 0});
    });
}

void UninstallCoordinator::execPruneDirs(const ueffect::PruneDirs& e) {
    QStringList dirs;
    for (const PayloadEntry& entry : manifest_.files) {
        QString rel = entry.path;
        int slash = rel.lastIndexOf(QLatin1Char('/'));
        while (slash > 0) {
            rel = rel.left(slash);
            const QString abs = e.dir + QLatin1Char('/') + rel;
            if (!dirs.contains(abs)) { dirs.append(abs); }
            slash = rel.lastIndexOf(QLatin1Char('/'));
        }
    }
    std::sort(dirs.begin(), dirs.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    runOnWorker([this, e, dirs] {
        for (const QString& dir : dirs) { fileOps_.removeDirIfEmpty(dir); }
        // The install dir itself usually survives (this process runs from
        // it); the helper removes it once empty.
        fileOps_.removeDirIfEmpty(e.dir);
        postEventFromWorker(uevent::EffectOk{effecttag::pruneDirs, 0});
    });
}

void UninstallCoordinator::execSpawnHelper() {
    const QString dir = state_.installDir;
    const QString scope = state_.scope;
    runOnWorker([this, dir, scope] {
        const QString temp = tempDir();
        if (temp.isEmpty()) {
            postEventFromWorker(
                uevent::EffectFail{effecttag::spawnHelper, SetupError::Internal, dir});
            return;
        }
        const QString hex =
            QString::number(QRandomGenerator::global()->generate(), 16).rightJustified(8, u'0');
        const QString helperDir = temp + QStringLiteral("/dish-uninstall-") + hex;
        const QString helperExe = helperDir + QStringLiteral("/uninstall-helper.exe");
        const QString listFile = helperDir + QStringLiteral("/leftovers.txt");
        const QString sourceHelper = dir + QStringLiteral("/uninstall-helper.exe");

        if (!fileOps_.ensureDir(helperDir).ok ||
            !fileOps_.copyWithProgress(sourceHelper, helperExe, nullptr).ok) {
            postEventFromWorker(
                uevent::EffectFail{effecttag::spawnHelper, SetupError::Internal, sourceHelper});
            return;
        }
        if (!writeUtf16List(listFile, leftovers_)) {
            postEventFromWorker(
                uevent::EffectFail{effecttag::spawnHelper, SetupError::Internal, listFile});
            return;
        }
        const QStringList argv{
            QStringLiteral("--waitpid"), QString::number(QCoreApplication::applicationPid()),
            QStringLiteral("--list"),    QDir::toNativeSeparators(listFile),
            QStringLiteral("--dir"),     QDir::toNativeSeparators(dir),
            QStringLiteral("--arp"),     scope,
        };
        // Same token on purpose: a machine-scope helper must stay elevated to
        // delete the HKLM ARP key and Program Files leftovers.
        const OpResult r = processOps_.launchDetached(helperExe, argv, helperDir, false);
        if (!r.ok) {
            postEventFromWorker(
                uevent::EffectFail{effecttag::spawnHelper, SetupError::Internal, helperExe});
            return;
        }
        completeOnGui([this, helperDir] {
            logger_.line(QStringLiteral("uninstall: helper spawned in \"%1\" (%2 leftovers)")
                             .arg(helperDir)
                             .arg(leftovers_.size()));
        });
        postEventFromWorker(uevent::EffectOk{effecttag::spawnHelper, 0});
    });
}

void UninstallCoordinator::execFinish(const ueffect::Finish& e) {
    completeOnGui([this, e] {
        executing_ = false;
        dispatch(uevent::EffectOk{effecttag::finish, 0});
        if (!finishedEmitted_) {
            finishedEmitted_ = true;
            logger_.line(QStringLiteral("uninstall: finished, exit %1 (%2)")
                             .arg(static_cast<int>(e.code))
                             .arg(QLatin1String(resultToken(e.code))));
            emit finished(e.code, state_.error, state_.errorPath);
        }
        pump();
    });
}

} // namespace dish::installer
