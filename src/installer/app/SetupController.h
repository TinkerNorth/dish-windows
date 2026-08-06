// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The QML facade of the installer (spec section 4, the binding contract the
// setup UI builds against). Registered BY INSTANCE in SetupMain
// (qmlRegisterSingletonInstance, the LTCG-safe QmlEntryPoint.cpp:57-71
// pattern); QML_NAMED_ELEMENT(Setup) + QML_SINGLETON exist so qmllint and
// qmlcachegen resolve the type from the generated qmltypes. The facade vends
// enums, tokens and pre-formatted sizes only — every user-facing sentence is a
// qsTr() in QML. Every destructive transition (force close, downgrade accept,
// uninstall, purge) happens only via an explicit invokable tied to an explicit
// user action.

#pragma once

#include "core/AsyncState.h"
#include "installer/CliOptions.h"
#include "installer/InstallCoordinator.h"
#include "installer/Logger.h"
#include "installer/Manifest.h"
#include "installer/UninstallCoordinator.h"
#include "installer/ops/Win32FileOps.h"
#include "installer/ops/Win32ProcessOps.h"
#include "installer/ops/Win32RegistryOps.h"
#include "installer/ops/Win32ShortcutOps.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>
#include <optional>

class QJSEngine;
class QQmlApplicationEngine;
class QQmlEngine;
class QTranslator;

namespace dish::installer {

class SetupController : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(Setup)
    QML_SINGLETON

  public:
    enum class Phase {
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
    Q_ENUM(Phase)
    enum class Mode { Install, Uninstall };
    Q_ENUM(Mode)
    enum class Scope { PerUser, AllUsers };
    Q_ENUM(Scope)
    enum class DirStatus {
        DirOk,
        DirNotAbsolute,
        DirDenied,
        DirInvalid,
        DirIsSystem,
        DirNotEmpty,
        DirIsExistingInstall,
    };
    Q_ENUM(DirStatus)
    enum class ErrorCode {
        NoError,
        Internal,
        Usage,
        UnsupportedOs,
        NeedElevation,
        AppRunning,
        DiskFull,
        PayloadCorrupt,
        FileOpFailed,
        RegistryFailed,
        ShortcutFailed,
        RollbackIncomplete,
        Cancelled,
        NothingInstalled,
        Downgrade,
        Busy,
        VersionMismatch,
    };
    Q_ENUM(ErrorCode)

    // Identity / mode (all CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT) // DISH_VERSION
    Q_PROPERTY(Mode mode READ mode CONSTANT)                // basename/flag decided
    Q_PROPERTY(bool isElevated READ isElevated CONSTANT)
    // Set only on the instance the elevation relaunch spawned: the user already
    // made every choice in the parent window, so the wizard commits instead of
    // asking again (spec D6).
    Q_PROPERTY(bool resumeInstall READ resumeInstall CONSTANT)
    Q_PROPERTY(QString logFilePath READ logFilePath CONSTANT)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    // ["system","en","bs","de","es","fr","pt_BR"]

    // Existing-install probe (NOTIFY probeChanged)
    Q_PROPERTY(bool existingDetected READ existingDetected NOTIFY probeChanged)
    Q_PROPERTY(QString existingVersion READ existingVersion NOTIFY probeChanged)
    Q_PROPERTY(QString existingDir READ existingDir NOTIFY probeChanged)
    Q_PROPERTY(Scope existingScope READ existingScope NOTIFY probeChanged)
    Q_PROPERTY(bool isDowngrade READ isDowngrade NOTIFY probeChanged)
    Q_PROPERTY(bool staleJournalFound READ staleJournalFound NOTIFY probeChanged)

    // Choices (NOTIFY optionsChanged; setters re-validate)
    Q_PROPERTY(Scope scope READ scope WRITE setScope NOTIFY optionsChanged)
    Q_PROPERTY(QString installDir READ installDir WRITE setInstallDir NOTIFY optionsChanged)
    Q_PROPERTY(DirStatus dirStatus READ dirStatus NOTIFY optionsChanged)
    Q_PROPERTY(bool needsElevation READ needsElevation NOTIFY optionsChanged)
    // scope==AllUsers && !isElevated
    Q_PROPERTY(bool wantStartMenu READ wantStartMenu WRITE setWantStartMenu NOTIFY optionsChanged)
    // default true
    Q_PROPERTY(bool wantDesktop READ wantDesktop WRITE setWantDesktop NOTIFY optionsChanged)
    // default false
    Q_PROPERTY(bool wantLaunch READ wantLaunch WRITE setWantLaunch NOTIFY optionsChanged)
    // default true (UI)
    Q_PROPERTY(bool wantPurgeUserData READ wantPurgeUserData WRITE setWantPurgeUserData NOTIFY
                   optionsChanged) // default false

    // Disk (NOTIFY diskChanged; AsyncState-backed probe)
    Q_PROPERTY(qint64 requiredBytes READ requiredBytes NOTIFY diskChanged)
    Q_PROPERTY(qint64 freeBytes READ freeBytes NOTIFY diskChanged)
    Q_PROPERTY(bool diskOk READ diskOk NOTIFY diskChanged)
    Q_PROPERTY(QString requiredText READ requiredText NOTIFY diskChanged) // formattedDataSize
    Q_PROPERTY(QString freeText READ freeText NOTIFY diskChanged)

    // Execution (NOTIFY phaseChanged / progressChanged, progress throttled ~30 Hz)
    Q_PROPERTY(Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(ErrorCode lastError READ lastError NOTIFY phaseChanged)
    Q_PROPERTY(QString lastErrorPath READ lastErrorPath NOTIFY phaseChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)        // 0..1
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY progressChanged) // relative path,
                                                                            // data not prose
    Q_PROPERTY(int fileIndex READ fileIndex NOTIFY progressChanged)         // 1-based
    Q_PROPERTY(int fileCount READ fileCount NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesCopied READ bytesCopied NOTIFY progressChanged)
    Q_PROPERTY(qint64 bytesTotal READ bytesTotal NOTIFY progressChanged)

    // Blockers (NOTIFY blockersChanged)
    Q_PROPERTY(bool appRunning READ appRunning NOTIFY blockersChanged)
    Q_PROPERTY(QStringList runningProcessNames READ runningProcessNames NOTIFY blockersChanged)
    Q_PROPERTY(int runningProcessCount READ runningProcessCount NOTIFY blockersChanged)

    // Language (NOTIFY languageChanged)
    Q_PROPERTY(QString uiLanguage READ uiLanguage WRITE setUiLanguage NOTIFY languageChanged)
    // "system" or a code; setter reinstalls the translator + engine->retranslate()

    // ── Non-QML wiring (SetupMain only) ────────────────────────────────────
    // `options` decides mode and pre-seeds every choice; `logger` must already
    // be open. Both must outlive the controller.
    SetupController(const CliOptions& options, Logger& logger, QObject* parent = nullptr);
    ~SetupController() override;

    // The engine (for retranslate()) and the installed translator (for the
    // Welcome language selector); called once right after construction.
    void attachRuntime(QQmlApplicationEngine* engine, QTranslator* translator);

    // The section 9 exit code SetupMain returns after app.exec(): Cancelled
    // until an operation concludes (closing the wizard IS a cancel), then the
    // concluded code.
    int exitCode() const { return exitCode_; }

    // qmltyperegistrar requires QML_SINGLETON types to be default-constructible
    // or expose create(); the real instance is built in SetupMain and
    // registered by instance, so create() only hands that instance back.
    static SetupController* create(QQmlEngine* engine, QJSEngine* jsEngine);

  public: // invokables
    Q_INVOKABLE QString defaultDirFor(Scope scope) const;
    Q_INVOKABLE QString browseForFolder(); // IFileOpenDialog FOS_PICKFOLDERS; "" on cancel
    Q_INVOKABLE QVariantList licenseEntries() const;
    // rows {id, name, version, spdx, text}: "lgpl3" (repo LICENSE), "gpl3"
    // (COPYING.GPL3), then :/licenses/licenses.json rows incl. miniz, Inter OFL
    Q_INVOKABLE QString languageDisplayName(const QString& code) const;
    Q_INVOKABLE void beginInstall(); // preflight onward; detours to elevation itself
    Q_INVOKABLE void beginUninstall();
    Q_INVOKABLE void cancel();                    // no-op during Committing/Finalizing/RollingBack
    Q_INVOKABLE void resolveBlockers(bool force); // false = WM_CLOSE + grace; true = terminate
    Q_INVOKABLE void rescanBlockers();
    Q_INVOKABLE void cleanStaleJournal(); // sweep a crashed previous attempt (probe offered it)
    Q_INVOKABLE void finishAndLaunch();   // Done page: launch dish.exe (de-elevated) + quit
    Q_INVOKABLE void finishOnly();
    Q_INVOKABLE void retry(); // Failed page: re-arm to the options step
    Q_INVOKABLE void openLogFile();
    Q_INVOKABLE void quitSetup();

  signals:
    void probeChanged();
    void optionsChanged();
    void diskChanged();
    void phaseChanged();
    void progressChanged();
    void blockersChanged();
    void languageChanged();
    void elevationDeclined(); // LocationPage callout trigger
    void installFinished(bool ok, ErrorCode error);
    void uninstallFinished(bool ok, ErrorCode error);

  public: // property readers
    QString appVersion() const;
    Mode mode() const { return mode_; }
    bool isElevated() const { return isElevated_; }
    bool resumeInstall() const { return options_.resumeInstall && mode_ == Mode::Install; }
    QString logFilePath() const { return logFilePath_; }
    QStringList availableLanguages() const;

    bool existingDetected() const { return existingDetected_; }
    QString existingVersion() const { return existingVersion_; }
    QString existingDir() const { return existingDir_; }
    Scope existingScope() const { return existingScope_; }
    bool isDowngrade() const { return isDowngrade_; }
    bool staleJournalFound() const { return staleJournalFound_; }

    Scope scope() const { return scope_; }
    void setScope(Scope scope);
    QString installDir() const { return installDir_; }
    void setInstallDir(const QString& dir);
    DirStatus dirStatus() const { return dirStatus_; }
    bool needsElevation() const { return scope_ == Scope::AllUsers && !isElevated_; }
    bool wantStartMenu() const { return wantStartMenu_; }
    void setWantStartMenu(bool want);
    bool wantDesktop() const { return wantDesktop_; }
    void setWantDesktop(bool want);
    bool wantLaunch() const { return wantLaunch_; }
    void setWantLaunch(bool want);
    bool wantPurgeUserData() const { return wantPurgeUserData_; }
    void setWantPurgeUserData(bool want);

    qint64 requiredBytes() const { return requiredBytes_; }
    qint64 freeBytes() const;
    bool diskOk() const;
    QString requiredText() const;
    QString freeText() const;

    Phase phase() const { return phase_; }
    ErrorCode lastError() const { return lastError_; }
    QString lastErrorPath() const { return lastErrorPath_; }
    double progress() const;
    QString currentFile() const { return currentFile_; }
    int fileIndex() const;
    int fileCount() const;
    qint64 bytesCopied() const { return bytesCopied_; }
    qint64 bytesTotal() const { return bytesTotal_; }

    bool appRunning() const { return !blockerNames_.isEmpty(); }
    QStringList runningProcessNames() const { return blockerNames_; }
    int runningProcessCount() const { return blockerNames_.size(); }

    QString uiLanguage() const { return uiLanguage_; }
    void setUiLanguage(const QString& code);

  private:
    void probeExistingInstall();
    void probeUninstallTarget();
    InstalledManifest bestEffortUninstallManifest() const;
    void kickDiskProbe();
    void setPhase(Phase phase);
    void emitProgressThrottled();
    QString planInstallDir() const;
    void sweepStaleJournalInline(const QString& installDir);
    void failBeforeStart(SetupError error, const QString& path);

    void onInstallPhase(InstallPhase phase);
    void onInstallProgress(qint64 done, qint64 total, const QString& rel);
    void onInstallFinished(ExitCode code, SetupError error, const QString& path);
    void onUninstallPhase(UninstallPhase phase);
    void onUninstallProgress(qint64 done, qint64 total, const QString& rel);
    void onUninstallFinished(ExitCode code, SetupError error, const QString& path);
    void onBlockers(const QVector<ProcInfo>& procs);

    CliOptions options_;
    Logger& logger_;
    Mode mode_ = Mode::Install;
    bool isElevated_ = false;
    QString logFilePath_;

    Win32FileOps fileOps_;
    Win32RegistryOps registryOps_;
    Win32ShortcutOps shortcutOps_;
    Win32ProcessOps processOps_;
    std::unique_ptr<InstallCoordinator> install_;
    std::unique_ptr<UninstallCoordinator> uninstall_;

    QQmlApplicationEngine* engine_ = nullptr;
    QTranslator* translator_ = nullptr;

    std::optional<PayloadManifest> payload_;
    std::optional<InstalledManifest> installed_; // upgrade target or uninstall record

    bool existingDetected_ = false;
    QString existingVersion_;
    QString existingDir_;
    Scope existingScope_ = Scope::PerUser;
    bool isDowngrade_ = false;
    bool staleJournalFound_ = false;

    Scope scope_ = Scope::PerUser;
    QString installDir_;
    DirStatus dirStatus_ = DirStatus::DirInvalid;
    bool wantStartMenu_ = true;
    bool wantDesktop_ = false;
    bool wantLaunch_ = true;
    bool wantPurgeUserData_ = false;

    // AsyncState so the UI can bind all four probe states instead of guessing
    // from a sentinel; Loading keeps the prior answer (stale) so the space
    // line never blanks while the user types.
    qint64 requiredBytes_ = 0;
    dish::core::AsyncState<qint64, SetupError> disk_;
    int diskGeneration_ = 0;
    QThread probeThread_;
    QObject* probeContext_ = nullptr;

    Phase phase_ = Phase::Idle;
    ErrorCode lastError_ = ErrorCode::NoError;
    QString lastErrorPath_;
    QString currentFile_;
    qint64 bytesCopied_ = 0;
    qint64 bytesTotal_ = 0;
    int uninstallIndex_ = 0;
    int uninstallCount_ = 0;
    QStringList blockerNames_;

    QString uiLanguage_ = QStringLiteral("system");

    QElapsedTimer progressClock_;
    QTimer progressTrailing_;
    bool progressDirty_ = false;

    bool elevationHandoff_ = false; // spawned an elevated instance; suppress Done
    int exitCode_ = static_cast<int>(ExitCode::Cancelled);

    static SetupController* instance_;
};

} // namespace dish::installer
