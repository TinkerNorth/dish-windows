// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The updater's coordinator: it owns the reducer's state as an Observable,
// turns world changes (timers, connectivity, gateway callbacks) into events,
// and executes the effects `reduceUpdate` hands back. The reducer decides;
// nothing here does.
//
// Threading. The reducer, this object and the manifest fetch live on the Qt
// main thread (a reduce is microseconds and the manifest is ~1 KB). One worker
// QThread named "dish-update" owns the payload download and ALL staging IO,
// because a QNetworkAccessManager belongs to the thread that created it and a
// 40 MB hash must never touch the frame loop. Nothing here goes near the input
// hot path.
//
// Laziness is a requirement, not an optimisation: while checks are disabled no
// QNAM is constructed at all, on either thread.

#pragma once

#include "architecture/Observable.h"
#include "core/reducer/UpdateMachine.h"
#include "source/store/UpdatePreferenceStore.h"
#include "update/UpdatePorts.h"

#include <QByteArray>
#include <QDateTime>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

class QThread;
class QTimer;

namespace dish::update {

// Injected wholesale by the test constructor. Any member left null falls back
// to the production implementation.
struct UpdateCoordinatorPorts {
    std::unique_ptr<ManifestGateway> manifest;
    std::unique_ptr<DownloadGateway> download;
    std::unique_ptr<StagingStore> staging;
};

class UpdateCoordinator : public QObject {
    Q_OBJECT
  public:
    // Production: real gateways, real staging directory, real worker thread.
    explicit UpdateCoordinator(source::UpdatePreferenceStore* prefs, QObject* parent = nullptr);

    // Tests: the supplied ports run entirely on the CALLING thread, no worker
    // is started, and every gateway callback is delivered inline. That makes a
    // whole update cycle assertable without spinning an event loop.
    UpdateCoordinator(source::UpdatePreferenceStore* prefs, UpdateCoordinatorPorts ports,
                      QObject* parent = nullptr);

    ~UpdateCoordinator() override;

    // The one reactive surface. AppViewModel subscribes; nothing else may.
    const arch::Observable<reducer::UpdateStatus>& status() const { return status_; }
    reducer::UpdateStatus snapshot() const { return status_.value(); }

    // Post-apply reconciliation ("updated to" edge + handoff key cleanup), the
    // janitor pass, the staged-update scan, and the startup schedule. Safe to
    // call once; a second call is ignored.
    void start();

    // Manual check: bypasses the interval and the backoff, rate-limited to one
    // per 10 s so a held-down button cannot hammer the permalink.
    void checkNow();

    // The Settings / popover "Download" action. Allowed on a metered link
    // (the user asked for it explicitly), never on a portable copy.
    void downloadNow();

    // Mutes status().availableVersion, discards any stage of it, and suppresses
    // restaging while the manifest still offers it. Ignored while required.
    void skipAvailableVersion();

    // Restart-now: arms the flag and leaves the closing to the caller, so every
    // existing QML close guard runs first. The spawn happens in the
    // aboutToQuit hook, never here.
    void armPendingRestart();
    bool pendingRestart() const { return pendingRestart_; }

    // Invalid when no check has ever completed.
    QDateTime lastCheck() const;

    // Non-empty for the session after a successful apply: the version this run
    // upgraded FROM. Consumed by acknowledgeUpdated().
    QString updatedFromVersion() const { return updatedFrom_; }
    void acknowledgeUpdated();

    // The armed check delay in ms, or -1 when no check is scheduled. Together
    // with firePendingCheck() this is the seam the schedule tests drive.
    int pendingCheckDelayMs() const { return pendingCheckDelayMs_; }
    void firePendingCheck();

    // Milliseconds since the Unix epoch, UTC. Injected so the 1 h minimum gap
    // and the 24 h future-jump escape are testable without touching the clock.
    using ClockFn = std::function<qint64()>;
    void setClock(ClockFn clock);

    // The running build, DISH_VERSION unless a test overrides it before start().
    void setCurrentVersion(const QString& version);

  signals:
    // Edge-detected, at most once per version per session. The facade maps the
    // enum to a token; the engine never vends a sentence.
    void notice(dish::reducer::UpdateNotice notice, const QString& version);

  private:
    void construct();
    void dispatch(const reducer::UpdateEvent& event);
    void execute(const reducer::UpdateEffect& effect);

    void reconcileAfterApply();
    void scanStaging();
    void scheduleStartupCheck();
    void hookConnectivity();
    void onAboutToQuit();

    // The manifest gateway is a main-thread object; the other two are only ever
    // touched from inside a runOnWorker job (which runs inline when untreaded).
    ManifestGateway* manifestGateway();
    StagingStore* stagingStore();
    DownloadGateway* downloadGateway();
    void runOnWorker(std::function<void()> job);
    void runOnMain(std::function<void()> job);
    bool threaded() const { return workerContext_ != nullptr; }

    qint64 nowMs() const;

    source::UpdatePreferenceStore* prefs_;
    arch::Observable<reducer::UpdateStatus> status_;
    arch::Observable<source::UpdatePreferences>::Subscription prefsSub_;

    UpdateCoordinatorPorts ports_;
    // Created lazily, and only ever on the thread that will use them: the
    // manifest gateway here, the download gateway on the worker. The staging
    // store has no thread affinity of its own (no QObject, no sockets); it is
    // built eagerly and then touched only from worker jobs.
    std::unique_ptr<ManifestGateway> manifestOwned_;
    std::unique_ptr<DownloadGateway> downloadOwned_;
    std::unique_ptr<StagingStore> stagingOwned_;
    QThread* worker_ = nullptr;
    // Lives on the worker; the pointer is written once under a blocking call
    // and only read afterwards.
    QObject* workerContext_ = nullptr;

    QTimer* checkTimer_ = nullptr;
    int pendingCheckDelayMs_ = -1;

    QSettings settings_;
    ClockFn clock_;

    // The bytes the current manifest arrived as, snapshotted next to a promoted
    // stage so a support request can see what the client was told.
    QByteArray manifestBody_;

    QString updatedFrom_;
    qint64 lastManualCheckMs_ = 0;
    bool pendingRestart_ = false;
    bool started_ = false;
    // Notices are once per version per session; the key is token+version.
    QStringList firedNotices_;
};

} // namespace dish::update
