// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Facade implementation: pre-seeds choices from the CLI, probes existing
// installs (HKCU before HKLM, spec 11.2), owns the two coordinators, and
// translates their typed signals into the QML property surface. All state
// lives on the GUI thread; the only worker here is the AsyncState-backed disk
// probe (spec 3.8) — every other IO belongs to the coordinators.

#include "installer/app/SetupController.h"

#include "installer/InstallPlan.h"
#include "installer/Journal.h"
#include "installer/VersionCompare.h"
#include "installer/ops/KnownFolders.h"
#include "Util/Localization.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QTranslator>
#include <QUrl>
#include <QVariantMap>
#include <QWindow>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shobjidl.h>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::installer {

namespace {

// The facade's Scope mirrors the engine's; the maps are spelled out so a
// reordering of either enum breaks loudly here instead of silently in QML.
Scope toEngine(SetupController::Scope scope) {
    return scope == SetupController::Scope::AllUsers ? Scope::AllUsers : Scope::PerUser;
}

SetupController::Scope toFacade(Scope scope) {
    return scope == Scope::AllUsers ? SetupController::Scope::AllUsers
                                    : SetupController::Scope::PerUser;
}

SetupController::DirStatus toFacade(DirStatus status) {
    switch (status) {
    case DirStatus::Ok:
        return SetupController::DirStatus::DirOk;
    case DirStatus::NotAbsolute:
        return SetupController::DirStatus::DirNotAbsolute;
    case DirStatus::Denied:
        return SetupController::DirStatus::DirDenied;
    case DirStatus::Invalid:
        return SetupController::DirStatus::DirInvalid;
    case DirStatus::IsSystem:
        return SetupController::DirStatus::DirIsSystem;
    case DirStatus::NotEmpty:
        return SetupController::DirStatus::DirNotEmpty;
    case DirStatus::IsExistingInstall:
        return SetupController::DirStatus::DirIsExistingInstall;
    }
    return SetupController::DirStatus::DirInvalid;
}

SetupController::ErrorCode toFacade(SetupError error) {
    switch (error) {
    case SetupError::None:
        return SetupController::ErrorCode::NoError;
    case SetupError::Internal:
        return SetupController::ErrorCode::Internal;
    case SetupError::Usage:
        return SetupController::ErrorCode::Usage;
    case SetupError::UnsupportedOs:
        return SetupController::ErrorCode::UnsupportedOs;
    case SetupError::NeedElevation:
        return SetupController::ErrorCode::NeedElevation;
    case SetupError::AppRunning:
        return SetupController::ErrorCode::AppRunning;
    case SetupError::DiskFull:
        return SetupController::ErrorCode::DiskFull;
    case SetupError::PayloadCorrupt:
        return SetupController::ErrorCode::PayloadCorrupt;
    case SetupError::FileOpFailed:
        return SetupController::ErrorCode::FileOpFailed;
    case SetupError::RegistryFailed:
        return SetupController::ErrorCode::RegistryFailed;
    case SetupError::ShortcutFailed:
        return SetupController::ErrorCode::ShortcutFailed;
    case SetupError::RollbackIncomplete:
        return SetupController::ErrorCode::RollbackIncomplete;
    case SetupError::Cancelled:
        return SetupController::ErrorCode::Cancelled;
    case SetupError::NothingInstalled:
        return SetupController::ErrorCode::NothingInstalled;
    case SetupError::Downgrade:
        return SetupController::ErrorCode::Downgrade;
    case SetupError::Busy:
        return SetupController::ErrorCode::Busy;
    case SetupError::VersionMismatch:
        return SetupController::ErrorCode::VersionMismatch;
    }
    return SetupController::ErrorCode::Internal;
}

SetupController::Phase toFacade(InstallPhase phase) {
    switch (phase) {
    case InstallPhase::Idle:
        return SetupController::Phase::Idle;
    case InstallPhase::Preflight:
        return SetupController::Phase::Preflight;
    case InstallPhase::AwaitingBlockers:
        return SetupController::Phase::AwaitingBlockers;
    case InstallPhase::AwaitingElevation:
        return SetupController::Phase::AwaitingElevation;
    case InstallPhase::Copying:
        return SetupController::Phase::Copying;
    case InstallPhase::Committing:
        return SetupController::Phase::Committing;
    case InstallPhase::Finalizing:
        return SetupController::Phase::Finalizing;
    case InstallPhase::RollingBack:
        return SetupController::Phase::RollingBack;
    case InstallPhase::Done:
        return SetupController::Phase::Done;
    case InstallPhase::Failed:
        return SetupController::Phase::Failed;
    }
    return SetupController::Phase::Idle;
}

// The shared Phase vocabulary has no uninstall-specific names (section 4);
// removal maps onto the copy/finalize shape the progress page already renders.
SetupController::Phase toFacade(UninstallPhase phase) {
    switch (phase) {
    case UninstallPhase::Idle:
        return SetupController::Phase::Idle;
    case UninstallPhase::Preflight:
        return SetupController::Phase::Preflight;
    case UninstallPhase::AwaitingBlockers:
        return SetupController::Phase::AwaitingBlockers;
    case UninstallPhase::RemovingShortcuts:
        return SetupController::Phase::Copying;
    case UninstallPhase::RemovingFiles:
        return SetupController::Phase::Copying;
    case UninstallPhase::PurgingData:
        return SetupController::Phase::Finalizing;
    case UninstallPhase::HandingOff:
        return SetupController::Phase::Finalizing;
    case UninstallPhase::Done:
        return SetupController::Phase::Done;
    case UninstallPhase::Failed:
        return SetupController::Phase::Failed;
    }
    return SetupController::Phase::Idle;
}

QString nativeClean(const QString& path) {
    return QDir::toNativeSeparators(QDir::cleanPath(QDir::fromNativeSeparators(path)));
}

std::optional<PayloadManifest> loadPayloadManifest(const QString& stagingDir) {
    QFile file(stagingDir + QStringLiteral("/manifest.json"));
    if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
    return PayloadManifest::fromJson(file.readAll());
}

std::optional<InstalledManifest> loadInstalledManifest(const QString& installDir) {
    QFile file(installDir + QLatin1Char('/') + installedManifestFileName());
    if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
    return InstalledManifest::fromJson(file.readAll());
}

// Does the .lnk this record claims still exist? The recorded absolute path
// wins over the freshly resolved one, so a known folder that moved after the
// install does not read as "the user deleted it". An unresolvable folder
// answers true: the choice is left alone rather than silently dropped.
bool recordedShortcutLives(ShortcutLocation location, Scope scope,
                           const InstalledManifest& installed, ShortcutOps& shortcutOps) {
    const QString resolved = shortcutLinkPath(location, scope);
    QString probe = resolved;
    for (const QString& path : installed.shortcutPaths) {
        if (QDir::cleanPath(QDir::fromNativeSeparators(path))
                .compare(QDir::cleanPath(QDir::fromNativeSeparators(resolved)),
                         Qt::CaseInsensitive) == 0) {
            probe = path;
            break;
        }
    }
    if (probe.isEmpty()) { return true; }
    return shortcutOps.exists(probe);
}

// Apartment COM for the folder picker; S_FALSE / RPC_E_CHANGED_MODE both mean
// "usable" (the GUI thread may already be initialized either way by Qt).
class ComApartment {
  public:
    ComApartment() : hr_(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(hr_)) { CoUninitialize(); }
    }
    bool ok() const { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

  private:
    HRESULT hr_;
};

} // namespace

SetupController* SetupController::instance_ = nullptr;

SetupController::SetupController(const CliOptions& options, Logger& logger, QObject* parent)
    : QObject(parent), options_(options), logger_(logger) {
    instance_ = this;
    mode_ = options.isUninstall() ? Mode::Uninstall : Mode::Install;
    isElevated_ = processOps_.isElevated();
    logFilePath_ = QDir::toNativeSeparators(logger_.path());
    uiLanguage_ = options.langOverride.isEmpty() ? QStringLiteral("system") : options.langOverride;

    progressTrailing_.setSingleShot(true);
    progressTrailing_.setInterval(33); // ~30 Hz trailing edge (spec section 4)
    connect(&progressTrailing_, &QTimer::timeout, this, [this] {
        if (progressDirty_) {
            progressDirty_ = false;
            progressClock_.restart();
            emit progressChanged();
        }
    });

    probeThread_.setObjectName(QStringLiteral("dish-setup-io"));
    probeContext_ = new QObject();
    probeContext_->moveToThread(&probeThread_);
    probeThread_.start();

    if (mode_ == Mode::Install) {
        payload_ = loadPayloadManifest(options_.stagingDir);
        if (!payload_) {
            logger_.line(QStringLiteral("controller: staged manifest.json missing or invalid"));
        }

        install_ = std::make_unique<InstallCoordinator>(fileOps_, registryOps_, shortcutOps_,
                                                        processOps_, logger_, this);
        install_->setStagingDir(options_.stagingDir);
        connect(install_.get(), &InstallCoordinator::phaseChanged, this,
                &SetupController::onInstallPhase);
        connect(install_.get(), &InstallCoordinator::progress, this,
                &SetupController::onInstallProgress);
        connect(install_.get(), &InstallCoordinator::blockers, this, &SetupController::onBlockers);
        connect(install_.get(), &InstallCoordinator::finished, this,
                &SetupController::onInstallFinished);
        connect(install_.get(), &InstallCoordinator::elevationDeclined, this, [this] {
            elevationHandoff_ = false;
            emit elevationDeclined();
        });
        connect(install_.get(), &InstallCoordinator::staleJournalRecovered, this, [this](bool) {
            staleJournalFound_ = false;
            emit probeChanged();
        });

        scope_ = toFacade(options_.plan.scope);
        wantStartMenu_ = options_.plan.startMenu;
        wantDesktop_ = options_.plan.desktop;
        wantLaunch_ = options_.plan.launch;
        installDir_ = options_.plan.installDir.isEmpty() ? defaultDirFor(scope_)
                                                         : nativeClean(options_.plan.installDir);
        probeExistingInstall();
    } else {
        uninstall_ = std::make_unique<UninstallCoordinator>(fileOps_, registryOps_, shortcutOps_,
                                                            processOps_, logger_, this);
        uninstall_->setClosePolicy(options_.plan.closePolicy);
        uninstall_->setWorkingSetProbe(
            [this](const QString& dir) { return processOps_.ownWorkingSetUnder(dir); });
        uninstall_->setPurgeRegistryHook([] { Win32RegistryOps::purgeUserSettingsTrees(); });
        connect(uninstall_.get(), &UninstallCoordinator::phaseChanged, this,
                &SetupController::onUninstallPhase);
        connect(uninstall_.get(), &UninstallCoordinator::progress, this,
                &SetupController::onUninstallProgress);
        connect(uninstall_.get(), &UninstallCoordinator::blockers, this,
                &SetupController::onBlockers);
        connect(uninstall_.get(), &UninstallCoordinator::finished, this,
                &SetupController::onUninstallFinished);

        wantPurgeUserData_ = options_.purgeUserData;
        probeUninstallTarget();
    }

    dirStatus_ = toFacade(validateInstallDir(installDir_));
    kickDiskProbe();
}

SetupController::~SetupController() {
    probeThread_.quit();
    probeThread_.wait();
    delete probeContext_;
    instance_ = nullptr;
}

void SetupController::attachRuntime(QQmlApplicationEngine* engine, QTranslator* translator) {
    engine_ = engine;
    translator_ = translator;
}

SetupController* SetupController::create(QQmlEngine* engine, QJSEngine* jsEngine) {
    Q_UNUSED(jsEngine);
    Q_UNUSED(engine);
    // SetupMain constructs and instance-registers the controller before any
    // engine load, so this path only runs if the auto-registration survived
    // LTCG and won the lookup; either way it must hand back the same object.
    if (instance_) { QQmlEngine::setObjectOwnership(instance_, QQmlEngine::CppOwnership); }
    return instance_;
}

// ── Probes ────────────────────────────────────────────────────────────────

void SetupController::probeExistingInstall() {
    // HKCU before HKLM: with installs in both hives the UI defaults per-user
    // (spec 11.2). The manifest at the recorded location is authoritative.
    for (const Scope probeScope : {Scope::PerUser, Scope::AllUsers}) {
        const auto arp = registryOps_.readInstalled(toEngine(probeScope));
        if (!arp) { continue; }
        const QString location = QDir::cleanPath(QDir::fromNativeSeparators(arp->installLocation));
        const auto recorded = loadInstalledManifest(location);
        if (!recorded) { continue; }
        installed_ = recorded;
        existingDetected_ = true;
        existingVersion_ = recorded->version;
        existingDir_ = QDir::toNativeSeparators(location);
        existingScope_ = toFacade(scopeFromToken(recorded->scope).value_or(toEngine(probeScope)));
        staleJournalFound_ = fileOps_.exists(journalFilePath(location));
        // Upgrades lock destination and scope to the record; moving is
        // uninstall + install (spec 11.2).
        scope_ = existingScope_;
        installDir_ = existingDir_;
        // Same rule pre-seeds the two switches: "from the recorded choices,
        // never re-create a deleted shortcut" (spec 11.2). Recorded AND still
        // on disk, so an upgrade neither resurrects a shortcut the user threw
        // away nor drops one they still have because the CLI default said off.
        const auto engineScope = scopeFromToken(recorded->scope).value_or(toEngine(probeScope));
        wantStartMenu_ =
            recorded->startMenu && recordedShortcutLives(ShortcutLocation::StartMenu, engineScope,
                                                         *recorded, shortcutOps_);
        wantDesktop_ =
            recorded->desktop &&
            recordedShortcutLives(ShortcutLocation::Desktop, engineScope, *recorded, shortcutOps_);
        break;
    }
    if (existingDetected_ && payload_) {
        const auto cmp = compareVersions(payload_->version, existingVersion_);
        isDowngrade_ = cmp && *cmp < 0;
    }
    emit probeChanged();
}

void SetupController::probeUninstallTarget() {
    const QString ownDir = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
    installed_ = loadInstalledManifest(ownDir);
    existingDetected_ = installed_.has_value();
    existingDir_ = QDir::toNativeSeparators(ownDir);
    installDir_ = existingDir_;
    if (installed_) {
        existingVersion_ = installed_->version;
        existingScope_ =
            toFacade(scopeFromToken(installed_->scope).value_or(toEngine(Scope::PerUser)));
        // Uninstall trusts where it actually runs from, like the silent path.
        installed_->installDir = ownDir;
        // The confirm page states the recorded install back to the user, so
        // both switches and the Size row have to come from the record rather
        // than from the install-side defaults. requiredBytes_ is payload-derived
        // and an uninstall carries no payload, which left Size (and the rail's
        // ON DISK) blank; the manifest already knows what was written.
        wantStartMenu_ = installed_->startMenu;
        wantDesktop_ = installed_->desktop;
        qint64 recordedBytes = 0;
        for (const PayloadEntry& entry : installed_->files) { recordedBytes += entry.size; }
        requiredBytes_ = recordedBytes;
    } else {
        logger_.line(QStringLiteral("controller: no .dish-manifest.json beside this exe; "
                                    "offering best-effort removal"));
    }
    scope_ = existingScope_;
    staleJournalFound_ = fileOps_.exists(journalFilePath(ownDir));
    emit probeChanged();
}

// The UI-offered fallback of spec 11.1 step 2: no record, so the removal set
// is what is actually on disk under this exe's directory right now, with the
// two-phase residue trees left to RemoveResidue.
InstalledManifest SetupController::bestEffortUninstallManifest() const {
    const QString ownDir = QDir::fromNativeSeparators(QCoreApplication::applicationDirPath());
    InstalledManifest manifest;
    manifest.schema = 1;
    manifest.version = QStringLiteral("0.0.0");
    manifest.installDir = ownDir;
    manifest.scope = QStringLiteral("user");
    for (const Scope probeScope : {Scope::PerUser, Scope::AllUsers}) {
        const auto arp =
            const_cast<Win32RegistryOps&>(registryOps_).readInstalled(toEngine(probeScope));
        if (!arp) { continue; }
        const QString location =
            QDir::cleanPath(QDir::fromNativeSeparators(arp->installLocation)).toCaseFolded();
        if (location == ownDir.toCaseFolded()) {
            manifest.scope = scopeToken(toEngine(probeScope));
            break;
        }
    }
    const auto engineScope = scopeFromToken(manifest.scope).value_or(toEngine(Scope::PerUser));
    for (const ShortcutLocation location :
         {ShortcutLocation::StartMenu, ShortcutLocation::Desktop}) {
        const QString link = shortcutLinkPath(location, engineScope);
        if (!link.isEmpty() && const_cast<Win32ShortcutOps&>(shortcutOps_).exists(link)) {
            manifest.shortcutPaths.append(link);
        }
    }
    const QDir root(ownDir);
    const QStringList files = const_cast<Win32FileOps&>(fileOps_).listRecursive(ownDir);
    for (const QString& abs : files) {
        const QString rel = root.relativeFilePath(abs);
        if (rel == installedManifestFileName() || rel == journalFileName() ||
            rel.startsWith(oldDirName() + QLatin1Char('/')) ||
            rel.startsWith(stageDirName() + QLatin1Char('/'))) {
            continue; // RemoveResidue owns these
        }
        PayloadEntry entry;
        entry.path = rel;
        entry.stagedAs = rel;
        entry.size = QFileInfo(abs).size();
        manifest.files.append(entry);
    }
    return manifest;
}

// ── Identity ──────────────────────────────────────────────────────────────

QString SetupController::appVersion() const { return QStringLiteral(DISH_VERSION); }

QStringList SetupController::availableLanguages() const {
    return {QStringLiteral("system"), QStringLiteral("en"), QStringLiteral("bs"),
            QStringLiteral("de"),     QStringLiteral("es"), QStringLiteral("fr"),
            QStringLiteral("pt_BR")};
}

// ── Choices ───────────────────────────────────────────────────────────────

void SetupController::setScope(Scope scope) {
    if (scope == scope_) { return; }
    if (mode_ == Mode::Install && existingDetected_) {
        logger_.line(QStringLiteral("controller: scope locked to the existing install"));
        return;
    }
    const QString oldDefault = defaultDirFor(scope_);
    scope_ = scope;
    // Only a still-default path follows the scope flip; a hand-picked one is
    // the user's and stays.
    if (installDir_.isEmpty() || nativeClean(installDir_) == nativeClean(oldDefault)) {
        installDir_ = defaultDirFor(scope_);
    }
    dirStatus_ = toFacade(validateInstallDir(installDir_));
    emit optionsChanged();
    kickDiskProbe();
}

void SetupController::setInstallDir(const QString& dir) {
    if (dir == installDir_) { return; }
    if (mode_ == Mode::Install && existingDetected_) {
        logger_.line(QStringLiteral("controller: install dir locked to the existing install"));
        return;
    }
    installDir_ = dir;
    dirStatus_ = toFacade(validateInstallDir(dir));
    emit optionsChanged();
    kickDiskProbe();
}

void SetupController::setWantStartMenu(bool want) {
    if (want == wantStartMenu_) { return; }
    wantStartMenu_ = want;
    emit optionsChanged();
}

void SetupController::setWantDesktop(bool want) {
    if (want == wantDesktop_) { return; }
    wantDesktop_ = want;
    emit optionsChanged();
}

void SetupController::setWantLaunch(bool want) {
    if (want == wantLaunch_) { return; }
    wantLaunch_ = want;
    emit optionsChanged();
}

void SetupController::setWantPurgeUserData(bool want) {
    if (want == wantPurgeUserData_) { return; }
    wantPurgeUserData_ = want;
    emit optionsChanged();
}

// ── Disk probe ────────────────────────────────────────────────────────────

QString SetupController::planInstallDir() const {
    return QDir::cleanPath(QDir::fromNativeSeparators(installDir_));
}

void SetupController::kickDiskProbe() {
    if (mode_ != Mode::Install) {
        // No payload to size, and nothing to probe: an uninstall needs no free
        // space. requiredBytes_ here is the RECORDED install size that
        // probeUninstallTarget() read out of the manifest, so recomputing it
        // from the (absent) payload would zero it and blank the confirm page's
        // Size row and the rail's ON DISK.
        emit diskChanged();
        return;
    }
    requiredBytes_ = payload_ ? payload_->totalBytes + kInstallDiskMargin : 0;
    const int generation = ++diskGeneration_;
    disk_ = dish::core::toLoading(disk_);
    emit diskChanged();
    const QString dir = planInstallDir();
    QMetaObject::invokeMethod(
        probeContext_,
        [this, generation, dir] {
            const qint64 free = fileOps_.freeBytesFor(dir);
            QMetaObject::invokeMethod(
                this,
                [this, generation, free] {
                    if (generation != diskGeneration_) { return; } // superseded
                    disk_ = free >= 0 ? dish::core::toSuccess(disk_, free)
                                      : dish::core::toError(disk_, SetupError::Internal);
                    emit diskChanged();
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
}

qint64 SetupController::freeBytes() const { return disk_.data.value_or(-1); }

bool SetupController::diskOk() const {
    if (requiredBytes_ <= 0) { return true; }
    // Unknown never blocks the wizard; the coordinator's ProbeDisk gate is the
    // enforcement point (mirrors its -1 rule).
    if (!disk_.data) { return true; }
    return *disk_.data >= requiredBytes_;
}

QString SetupController::requiredText() const {
    if (requiredBytes_ <= 0) { return QString(); }
    return QLocale().formattedDataSize(requiredBytes_);
}

QString SetupController::freeText() const {
    if (!disk_.data) { return QString(); }
    return QLocale().formattedDataSize(*disk_.data);
}

// ── Execution surface ─────────────────────────────────────────────────────

double SetupController::progress() const {
    if (mode_ == Mode::Uninstall) {
        return uninstallCount_ > 0
                   ? static_cast<double>(uninstallIndex_) / static_cast<double>(uninstallCount_)
                   : 0.0;
    }
    return bytesTotal_ > 0 ? static_cast<double>(bytesCopied_) / static_cast<double>(bytesTotal_)
                           : 0.0;
}

int SetupController::fileIndex() const {
    const int count = fileCount();
    if (count <= 0) { return 0; }
    const int completed =
        mode_ == Mode::Uninstall ? uninstallIndex_ : (install_ ? install_->state().fileIndex : 0);
    // "file %1 of %2" names the file in flight: completed + 1, capped so the
    // last file does not read "N+1 of N".
    return qMin(completed + 1, count);
}

int SetupController::fileCount() const {
    if (mode_ == Mode::Uninstall) { return uninstallCount_; }
    return payload_ ? payload_->files.size() : 0;
}

void SetupController::setPhase(Phase phase) {
    if (phase == phase_) { return; }
    phase_ = phase;
    emit phaseChanged();
}

void SetupController::emitProgressThrottled() {
    if (!progressClock_.isValid() || progressClock_.elapsed() >= 33) {
        progressClock_.restart();
        progressDirty_ = false;
        emit progressChanged();
        return;
    }
    progressDirty_ = true;
    if (!progressTrailing_.isActive()) { progressTrailing_.start(); }
}

void SetupController::onInstallPhase(InstallPhase phase) {
    lastError_ = toFacade(install_->state().error);
    lastErrorPath_ = install_->state().errorPath;
    if (elevationHandoff_ && phase == InstallPhase::Done) {
        return; // the elevated instance owns the install; this one is quitting
    }
    setPhase(toFacade(phase));
    if (phase == InstallPhase::Done || phase == InstallPhase::Failed) {
        emit progressChanged(); // final values, unthrottled
    }
}

void SetupController::onInstallProgress(qint64 done, qint64 total, const QString& rel) {
    bytesCopied_ = done;
    bytesTotal_ = total;
    if (!rel.isEmpty()) { currentFile_ = rel; }
    emitProgressThrottled();
}

void SetupController::onInstallFinished(ExitCode code, SetupError error, const QString& path) {
    exitCode_ = static_cast<int>(code);
    if (elevationHandoff_) {
        // AwaitingElevation handed the install to the elevated child; report
        // its spawn as our outcome and leave (spec D6).
        quitSetup();
        return;
    }
    lastError_ = toFacade(error);
    lastErrorPath_ = path;
    emit phaseChanged(); // error properties ride phaseChanged
    emit installFinished(code == ExitCode::Ok, lastError_);
}

void SetupController::onUninstallPhase(UninstallPhase phase) {
    lastError_ = toFacade(uninstall_->state().error);
    lastErrorPath_ = uninstall_->state().errorPath;
    setPhase(toFacade(phase));
}

void SetupController::onUninstallProgress(qint64 done, qint64 total, const QString& rel) {
    uninstallIndex_ = static_cast<int>(done);
    uninstallCount_ = static_cast<int>(total);
    if (!rel.isEmpty()) { currentFile_ = rel; }
    emitProgressThrottled();
}

void SetupController::onUninstallFinished(ExitCode code, SetupError error, const QString& path) {
    exitCode_ = static_cast<int>(code);
    lastError_ = toFacade(error);
    lastErrorPath_ = path;
    emit phaseChanged();
    emit uninstallFinished(code == ExitCode::Ok, lastError_);
}

void SetupController::onBlockers(const QVector<ProcInfo>& procs) {
    QStringList names;
    names.reserve(procs.size());
    for (const ProcInfo& proc : procs) {
        names.append(proc.name.isEmpty() ? QFileInfo(proc.imagePath).fileName() : proc.name);
    }
    blockerNames_ = names;
    emit blockersChanged();
}

// ── Invokables ────────────────────────────────────────────────────────────

QString SetupController::defaultDirFor(Scope scope) const {
    return QDir::toNativeSeparators(defaultInstallDir(toEngine(scope)));
}

QString SetupController::browseForFolder() {
    ComApartment com;
    if (!com.ok()) { return QString(); }
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&dialog)))) {
        return QString();
    }
    QString picked;
    DWORD flags = 0;
    dialog->GetOptions(&flags);
    dialog->SetOptions(flags | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);

    HWND owner = nullptr;
    if (const QWindow* window = QGuiApplication::focusWindow()) {
        owner = reinterpret_cast<HWND>(window->winId());
    } else if (!QGuiApplication::topLevelWindows().isEmpty()) {
        owner = reinterpret_cast<HWND>(QGuiApplication::topLevelWindows().first()->winId());
    }
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR raw = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &raw)) && raw) {
                picked = QString::fromWCharArray(raw);
                CoTaskMemFree(raw);
            }
            item->Release();
        }
    }
    dialog->Release();
    return picked; // native separators; "" on cancel
}

QString SetupController::languageDisplayName(const QString& code) const {
    // "system" is deliberately empty: its label is a sentence, and sentences
    // are qsTr() in QML, never vended from here.
    if (code == QLatin1String("system")) { return QString(); }
    const QLocale locale(code);
    QString name = locale.nativeLanguageName();
    if (name.isEmpty()) { return code; }
    if (code.contains(QLatin1Char('_'))) {
        const QString territory = locale.nativeTerritoryName();
        if (!territory.isEmpty()) { name += QStringLiteral(" (") + territory + QLatin1Char(')'); }
    }
    name[0] = name.at(0).toUpper();
    return name;
}

void SetupController::failBeforeStart(SetupError error, const QString& path) {
    lastError_ = toFacade(error);
    lastErrorPath_ = path;
    exitCode_ = static_cast<int>(toExitCode(error));
    setPhase(Phase::Failed);
    emit installFinished(false, lastError_);
}

void SetupController::sweepStaleJournalInline(const QString& installDir) {
    if (!fileOps_.exists(journalFilePath(installDir))) { return; }
    logger_.line(QStringLiteral("controller: recovering stale journal before start"));
    recoverStaleJournal(installDir, fileOps_, registryOps_, shortcutOps_);
    staleJournalFound_ = false;
    emit probeChanged();
}

void SetupController::beginInstall() {
    if (mode_ != Mode::Install || !install_) { return; }
    if (!payload_) {
        failBeforeStart(SetupError::PayloadCorrupt, options_.stagingDir);
        return;
    }
    // DirNotEmpty is advice here, not a verdict. The Location page shows the
    // user exactly what continuing does ("Files with matching names will be
    // replaced") and they pressed Install anyway; refusing after that would
    // make the sentence a lie and would also block repairing an install whose
    // manifest went missing. SilentRunner keeps the stricter rule on purpose:
    // an unattended run has nobody to read the warning.
    if (dirStatus_ != DirStatus::DirOk && dirStatus_ != DirStatus::DirIsExistingInstall &&
        dirStatus_ != DirStatus::DirNotEmpty) {
        logger_.line(QStringLiteral("controller: beginInstall with invalid dir ignored"));
        return;
    }

    InstallPlan plan;
    plan.scope = toEngine(scope_);
    plan.installDir = planInstallDir();
    plan.startMenu = wantStartMenu_;
    plan.desktop = wantDesktop_;
    // The Done page decides the launch (finishAndLaunch/finishOnly), so the
    // pipeline itself never spawns; wantLaunch only pre-seeds that switch.
    plan.launch = false;
    // A downgrade only reaches here through the explicit confirm dialog the
    // wizard shows first (spec section 4 contract notes).
    plan.allowDowngrade = isDowngrade_;
    plan.closePolicy = ClosePolicy::Abort; // the UI resolves blockers itself

    if (existingDetected_ && planInstallDir().compare(QDir::fromNativeSeparators(existingDir_),
                                                      Qt::CaseInsensitive) == 0) {
        plan.isUpgrade = true;
        plan.existingVersion = existingVersion_;
        plan.existingDir = QDir::fromNativeSeparators(existingDir_);
        install_->setOldManifest(installed_);
    } else {
        install_->setOldManifest(std::nullopt);
    }

    if (needsElevation()) {
        // D6: relaunch the ORIGINAL dish-setup.exe elevated with the full
        // choice set; the elevated instance re-runs the stub pipeline into its
        // own %TEMP% and re-opens pre-seeded. A dev-run UI exe (no stub)
        // relaunches itself and keeps its staging.
        CliOptions relaunch = options_;
        relaunch.mode = CliOptions::Mode::UiInstall;
        relaunch.plan = plan;
        relaunch.plan.launch = wantLaunch_;
        relaunch.plan.isUpgrade = false; // probe-derived; the child re-probes
        relaunch.plan.existingVersion.clear();
        relaunch.plan.existingDir.clear();
        relaunch.elevated = true;
        // Without this the elevated instance re-opens on Welcome with the
        // choices merely pre-seeded, and the user has to walk the whole wizard
        // and press Install a second time to get the install they already
        // approved at the UAC prompt. It commits straight away instead.
        relaunch.resumeInstall = true;
        relaunch.purgeUserData = false;
        QString exe = options_.sourceExe;
        if (!exe.isEmpty()) {
            relaunch.stagingDir.clear();
            relaunch.sourceExe.clear();
        } else {
            exe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
        }
        install_->setElevationRelaunch(QDir::toNativeSeparators(exe), relaunch.toArgv());
        elevationHandoff_ = true;
        install_->requestElevatedRestart();
        return;
    }

    sweepStaleJournalInline(plan.installDir);
    install_->start(plan, *payload_);
}

void SetupController::beginUninstall() {
    if (mode_ != Mode::Uninstall || !uninstall_) { return; }

    InstalledManifest manifest = installed_ ? *installed_ : bestEffortUninstallManifest();
    const auto engineScope = scopeFromToken(manifest.scope).value_or(toEngine(Scope::PerUser));

    if (engineScope == toEngine(Scope::AllUsers) && !isElevated_) {
        // Spec 11.1 step 3: the UI relaunches itself elevated with identical
        // args; the elevated instance re-confirms and runs.
        CliOptions relaunch = options_;
        relaunch.mode = CliOptions::Mode::UiUninstall;
        relaunch.purgeUserData = wantPurgeUserData_;
        relaunch.elevated = true;
        const QString exe = QDir::fromNativeSeparators(QCoreApplication::applicationFilePath());
        const OpResult spawned =
            processOps_.relaunchElevated(QDir::toNativeSeparators(exe), relaunch.toArgv());
        if (spawned.ok) {
            exitCode_ = static_cast<int>(ExitCode::Ok);
            quitSetup();
        } else {
            logger_.line(QStringLiteral("controller: uninstall elevation declined"));
            emit elevationDeclined();
        }
        return;
    }

    const QString dir = QDir::fromNativeSeparators(existingDir_);
    if (fileOps_.exists(journalFilePath(dir))) {
        logger_.line(QStringLiteral("controller: recovering stale journal before uninstall"));
        recoverStaleJournal(dir, fileOps_, registryOps_, shortcutOps_);
        staleJournalFound_ = false;
        emit probeChanged();
    }
    uninstall_->setPurgeUserData(wantPurgeUserData_);
    uninstall_->start(manifest);
}

void SetupController::cancel() {
    if (mode_ == Mode::Install && install_) {
        install_->requestCancel();
    } else if (uninstall_) {
        uninstall_->requestCancel();
    }
}

void SetupController::resolveBlockers(bool force) {
    if (mode_ == Mode::Install && install_) {
        install_->resolveBlockers(force);
    } else if (uninstall_) {
        uninstall_->resolveBlockers(force);
    }
}

void SetupController::rescanBlockers() {
    if (mode_ == Mode::Install && install_) {
        install_->rescanBlockers();
    } else if (uninstall_) {
        uninstall_->rescanBlockers();
    }
}

void SetupController::cleanStaleJournal() {
    const QString dir =
        existingDetected_ ? QDir::fromNativeSeparators(existingDir_) : planInstallDir();
    if (mode_ == Mode::Install && install_) {
        install_->recoverStaleJournalAt(dir); // staleJournalRecovered updates the probe
        return;
    }
    recoverStaleJournal(dir, fileOps_, registryOps_, shortcutOps_);
    staleJournalFound_ = false;
    emit probeChanged();
}

void SetupController::finishAndLaunch() {
    if (install_) { install_->launchInstalledApp(); }
    quitSetup();
}

void SetupController::finishOnly() { quitSetup(); }

void SetupController::retry() {
    if (mode_ != Mode::Install) { return; }
    lastError_ = ErrorCode::NoError;
    lastErrorPath_.clear();
    bytesCopied_ = 0;
    currentFile_.clear();
    setPhase(Phase::Idle);
    emit progressChanged();
    kickDiskProbe(); // the failed attempt may have changed the disk picture
}

void SetupController::openLogFile() {
    if (logFilePath_.isEmpty()) { return; }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir::fromNativeSeparators(logFilePath_)));
}

void SetupController::quitSetup() { QCoreApplication::quit(); }

// ── Language ──────────────────────────────────────────────────────────────

void SetupController::setUiLanguage(const QString& code) {
    if (code == uiLanguage_ || !availableLanguages().contains(code)) { return; }
    uiLanguage_ = code;
    const QLocale locale = code == QLatin1String("system") ? QLocale::system() : QLocale(code);
    QLocale::setDefault(locale); // formattedDataSize etc. follow the choice
    if (translator_) {
        QCoreApplication::removeTranslator(translator_);
        if (dish::i18n::loadCatalog(*translator_, locale)) {
            QCoreApplication::installTranslator(translator_);
        }
    }
    if (engine_) { engine_->retranslate(); }
    logger_.line(QStringLiteral("controller: ui language -> %1").arg(code));
    emit languageChanged();
    emit diskChanged(); // the pre-formatted size strings re-localize
}

} // namespace dish::installer
