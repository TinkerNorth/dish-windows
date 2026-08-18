// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "update/UpdateCoordinator.h"

#include "core/update/UpdateVersion.h"
#include "update/FileStagingStore.h"
#include "update/HttpGateways.h"
#include "update/UpdateHandoff.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QNetworkInformation>
#include <QRandomGenerator>
#include <QThread>
#include <QTimeZone>
#include <QTimer>

#include <utility>

#ifndef DISH_VERSION
#define DISH_VERSION "0.0.0"
#endif

namespace dish::update {

namespace {

// The portable/managed probe: an Inno Setup uninstaller (unins*.exe) beside
// dish.exe. Filesystem-only on purpose — an ARP lookup would couple the
// updater to registry state a half-broken uninstall could strand. A glob
// rather than unins000.exe exactly: Inno numbers upward when an older
// uninstaller is still present in the directory.
bool detectManagedInstall() {
    QString exeDir;
    if (QCoreApplication::instance() != nullptr) {
        exeDir = QCoreApplication::applicationDirPath();
    } else {
        exeDir = QFileInfo(UpdateHandoff::runningExecutablePath()).absolutePath();
    }
    if (exeDir.isEmpty()) { return false; }
    return !QDir(exeDir)
                .entryList(QStringList{QStringLiteral("unins*.exe")}, QDir::Files)
                .isEmpty();
}

QString noticeKey(reducer::UpdateNotice notice, const QString& version) {
    return QString::number(static_cast<int>(notice)) + QLatin1Char('/') + version;
}

} // namespace

UpdateCoordinator::UpdateCoordinator(source::UpdatePreferenceStore* prefs, QObject* parent)
    : QObject(parent), prefs_(prefs), status_(reducer::UpdateStatus{}) {
    stagingOwned_ = std::make_unique<FileStagingStore>();

    worker_ = new QThread(this);
    worker_->setObjectName(QStringLiteral("dish-update"));
    worker_->start();
    // Parentless: it is deleted from inside the worker during teardown, and a
    // QObject may only be destroyed on its own thread.
    workerContext_ = new QObject();
    workerContext_->moveToThread(worker_);

    construct();
}

UpdateCoordinator::UpdateCoordinator(source::UpdatePreferenceStore* prefs,
                                     UpdateCoordinatorPorts ports, QObject* parent)
    : QObject(parent), prefs_(prefs), status_(reducer::UpdateStatus{}), ports_(std::move(ports)) {
    if (!ports_.staging) { stagingOwned_ = std::make_unique<FileStagingStore>(); }
    construct();
}

void UpdateCoordinator::construct() {
    checkTimer_ = new QTimer(this);
    checkTimer_->setSingleShot(true);
    QObject::connect(checkTimer_, &QTimer::timeout, this, [this] {
        pendingCheckDelayMs_ = -1;
        dispatch(reducer::update_event::CheckRequested{reducer::UpdateTrigger::Periodic});
    });

    reducer::UpdateStatus initial;
    initial.currentVersion = QString::fromLatin1(DISH_VERSION);
    initial.portable = !detectManagedInstall();
    if (prefs_ != nullptr) {
        const auto values = prefs_->state().value();
        initial.checksEnabled = values.checksEnabled;
        initial.autoDownload = values.autoDownload;
        initial.skippedVersion = values.skippedVersion;
        initial.phase =
            values.checksEnabled ? reducer::UpdatePhase::Idle : reducer::UpdatePhase::Disabled;
    }

    // A version that burned both apply attempts surfaces as ApplyFailed until
    // the next successful check replaces it. The stage itself is already gone
    // (UpdateHandoff::quarantine deleted it and muted the version).
    const QString handoffVersion =
        settings_.value(QLatin1String(source::kKeyUpdatesHandoffVersion)).toString();
    const int handoffAttempts =
        settings_.value(QLatin1String(source::kKeyUpdatesHandoffAttempts), 0).toInt();
    if (!handoffVersion.isEmpty() && isStrictlyNewer(handoffVersion, initial.currentVersion) &&
        handoffAttempts >= reducer::kMaxApplyAttemptsPerVersion) {
        initial.phase = initial.checksEnabled ? reducer::UpdatePhase::Failed : initial.phase;
        initial.error = reducer::UpdateError::ApplyFailed;
        initial.availableVersion = handoffVersion;
    }
    status_.set(initial);

    if (prefs_ != nullptr) {
        // emitCurrent=false: the initial slice is already folded in above, and
        // a synthetic PrefsChanged here would re-arm the startup schedule.
        prefsSub_ = prefs_->state().subscribe(
            [this](const source::UpdatePreferences& values) {
                dispatch(reducer::update_event::PrefsChanged{
                    values.checksEnabled, values.autoDownload, values.skippedVersion});
            },
            false);
    }

    if (QCoreApplication::instance() != nullptr) {
        QObject::connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this,
                         [this] { onAboutToQuit(); });
    }
}

UpdateCoordinator::~UpdateCoordinator() {
    checkTimer_->stop();
    if (worker_ != nullptr) {
        QObject* context = workerContext_;
        // Both objects belong to the worker, so both die there. The blocking
        // call also guarantees no job is mid-flight when the thread stops: it
        // queues BEHIND every job already posted, so those run first.
        //
        // workerContext_ stays non-null for the whole wait, and that is the
        // point. Those still-queued jobs call runOnMain(), which asks
        // threaded() whether to post or to run inline — clearing the pointer
        // first would answer "not threaded" and run a dispatch() on the WORKER,
        // mutating the status Observable, QSettings and checkTimer_ from the
        // wrong thread. Posted main-thread jobs are simply discarded instead
        // when this object's ~QObject drops its event queue, which is exactly
        // what a coordinator being destroyed wants.
        QMetaObject::invokeMethod(
            context,
            [this, context] {
                downloadOwned_.reset();
                delete context;
            },
            Qt::BlockingQueuedConnection);
        workerContext_ = nullptr;
        worker_->quit();
        worker_->wait();
    }
}

// ── Public commands ─────────────────────────────────────────────────────────

void UpdateCoordinator::start() {
    if (started_) { return; }
    started_ = true;

    reconcileAfterApply();
    hookConnectivity();

    // The janitor runs before the scan, so a stage the sweep condemns is never
    // reported as found.
    const QString current = status_.value().currentVersion;
    runOnWorker([this, current] {
        if (auto* staging = stagingStore()) { staging->sweep(current); }
    });
    scanStaging();
    scheduleStartupCheck();
}

void UpdateCoordinator::checkNow() {
    const qint64 now = nowMs();
    if (lastManualCheckMs_ != 0 && now - lastManualCheckMs_ < reducer::kManualMinGapMs) { return; }
    lastManualCheckMs_ = now;
    checkTimer_->stop();
    pendingCheckDelayMs_ = -1;
    dispatch(reducer::update_event::CheckRequested{reducer::UpdateTrigger::Manual});
}

void UpdateCoordinator::downloadNow() { dispatch(reducer::update_event::DownloadRequested{}); }

void UpdateCoordinator::skipAvailableVersion() {
    const reducer::UpdateStatus current = status_.value();
    if (current.availableVersion.isEmpty() || current.required) { return; }
    // The store write republishes as PrefsChanged; the event below is what
    // discards the stage and mutes the surfaces.
    if (prefs_ != nullptr) { prefs_->setSkippedVersion(current.availableVersion); }
    dispatch(reducer::update_event::SkipRequested{current.availableVersion});
}

void UpdateCoordinator::armPendingRestart() { pendingRestart_ = true; }

QDateTime UpdateCoordinator::lastCheck() const {
    const qint64 ms =
        settings_.value(QLatin1String(source::kKeyUpdatesLastCheckUtcMs), 0).toLongLong();
    if (ms <= 0) { return {}; }
    return QDateTime::fromMSecsSinceEpoch(ms, QTimeZone::UTC);
}

void UpdateCoordinator::acknowledgeUpdated() { updatedFrom_.clear(); }

void UpdateCoordinator::firePendingCheck() {
    if (pendingCheckDelayMs_ < 0) { return; }
    checkTimer_->stop();
    pendingCheckDelayMs_ = -1;
    dispatch(reducer::update_event::CheckRequested{reducer::UpdateTrigger::Periodic});
}

void UpdateCoordinator::setClock(ClockFn clock) { clock_ = std::move(clock); }

void UpdateCoordinator::setCurrentVersion(const QString& version) {
    reducer::UpdateStatus next = status_.value();
    next.currentVersion = version;
    status_.set(next);
}

qint64 UpdateCoordinator::nowMs() const {
    return clock_ ? clock_() : QDateTime::currentMSecsSinceEpoch();
}

// ── Startup sequence ────────────────────────────────────────────────────────

void UpdateCoordinator::reconcileAfterApply() {
    const QString current = status_.value().currentVersion;

    // The handoff record is spent once this build has caught up with it, which
    // is also how a SUCCESSFUL apply is detected: nothing else reports it.
    const QString handoffVersion =
        settings_.value(QLatin1String(source::kKeyUpdatesHandoffVersion)).toString();
    if (!handoffVersion.isEmpty() && !isStrictlyNewer(handoffVersion, current)) {
        settings_.remove(QLatin1String(source::kKeyUpdatesHandoffVersion));
        settings_.remove(QLatin1String(source::kKeyUpdatesHandoffAttempts));
    }

    const QString lastRun =
        settings_.value(QLatin1String(source::kKeyUpdatesLastRunVersion)).toString();
    if (lastRun != current) {
        settings_.setValue(QLatin1String(source::kKeyUpdatesLastRunVersion), current);
        settings_.sync();
        // Only a forward move is a moment worth announcing; a downgrade (a
        // manual reinstall of an older build) passes in silence.
        if (!lastRun.isEmpty() && isStrictlyNewer(current, lastRun)) {
            updatedFrom_ = lastRun;
            const QString key = noticeKey(reducer::UpdateNotice::Updated, current);
            if (!firedNotices_.contains(key)) {
                firedNotices_.append(key);
                emit notice(reducer::UpdateNotice::Updated, current);
            }
        }
    }
}

void UpdateCoordinator::scanStaging() {
    runOnWorker([this] {
        auto* staging = stagingStore();
        if (staging == nullptr) { return; }
        auto staged = staging->findStaged();
        if (!staged.has_value()) { return; }
        const QString version = staged->version;
        const QString dir = staged->dir;
        runOnMain(
            [this, version, dir] { dispatch(reducer::update_event::StagedFound{version, dir}); });
    });
}

void UpdateCoordinator::scheduleStartupCheck() {
    if (!status_.value().checksEnabled) { return; }

    const qint64 now = nowMs();
    const qint64 last =
        settings_.value(QLatin1String(source::kKeyUpdatesLastCheckUtcMs), 0).toLongLong();
    const qint64 sinceLast = now - last;
    // A recorded time far in the FUTURE means the clock moved, not that a check
    // just happened; check anyway rather than going quiet for a day.
    const bool clockJumped = last > now + reducer::kFutureSkewEscapeMs;
    const bool withinGap = last > 0 && !clockJumped && sinceLast < reducer::kMinCheckGapMs;

    checkTimer_->stop();
    pendingCheckDelayMs_ = withinGap ? reducer::kPeriodicIntervalMs : reducer::kStartupDelayMs;
    checkTimer_->start(pendingCheckDelayMs_);
}

void UpdateCoordinator::hookConnectivity() {
    // A backend that will not load degrades to always-attempt: worst case the
    // request fails and backs off, which is strictly better than never trying.
    if (!QNetworkInformation::loadDefaultBackend()) { return; }
    auto* info = QNetworkInformation::instance();
    if (info == nullptr) { return; }

    const auto readOnline = [](QNetworkInformation* source) {
        if (source->supports(QNetworkInformation::Feature::CaptivePortal) &&
            source->isBehindCaptivePortal()) {
            return false;
        }
        if (!source->supports(QNetworkInformation::Feature::Reachability)) { return true; }
        return source->reachability() == QNetworkInformation::Reachability::Online;
    };

    dispatch(reducer::update_event::ReachabilityChanged{readOnline(info)});
    if (info->supports(QNetworkInformation::Feature::Metered)) {
        dispatch(reducer::update_event::MeteredChanged{info->isMetered()});
    }

    QObject::connect(info, &QNetworkInformation::reachabilityChanged, this,
                     [this, info, readOnline](QNetworkInformation::Reachability) {
                         dispatch(reducer::update_event::ReachabilityChanged{readOnline(info)});
                     });
    QObject::connect(info, &QNetworkInformation::isBehindCaptivePortalChanged, this,
                     [this, info, readOnline](bool) {
                         dispatch(reducer::update_event::ReachabilityChanged{readOnline(info)});
                     });
    QObject::connect(info, &QNetworkInformation::isMeteredChanged, this, [this](bool metered) {
        dispatch(reducer::update_event::MeteredChanged{metered});
    });
}

void UpdateCoordinator::onAboutToQuit() {
    if (!pendingRestart_) { return; }
    pendingRestart_ = false;
    // The FULL guard set again, on the way out: the stage may have been swept,
    // corrupted or superseded since the button was pressed, and a same-moment
    // re-hash is the only honest answer.
    auto staged = UpdateHandoff::verifiedStageForApply(status_.value().currentVersion);
    if (!staged.has_value()) { return; }
    if (!UpdateHandoff::recordAttempt(*staged)) {
        UpdateHandoff::quarantine(*staged);
        return;
    }
    // Failure here is not recoverable from a quitting process; the next boot's
    // gate retries (attempt 2) or quarantines.
    (void)UpdateHandoff::spawnStagedApply(*staged);
}

// ── Reducer plumbing ────────────────────────────────────────────────────────

void UpdateCoordinator::dispatch(const reducer::UpdateEvent& event) {
    reducer::UpdateReduction reduction = reducer::reduceUpdate(status_.value(), event);
    // The state moves BEFORE the effects run, so an effect that completes
    // synchronously (every fake gateway, and a cached staging read) re-enters
    // dispatch against the new state rather than the old one.
    status_.set(reduction.next);
    for (const reducer::UpdateEffect& effect : reduction.effects) { execute(effect); }
}

void UpdateCoordinator::execute(const reducer::UpdateEffect& effect) {
    if (std::get_if<reducer::update_effect::FetchManifest>(&effect) != nullptr) {
        // `manual` only relaxes checkNow()'s rate limit, which has already been
        // applied by the time the effect reaches here; the request is identical.
        auto* gateway = manifestGateway();
        if (gateway == nullptr) {
            dispatch(reducer::update_event::CheckFailed{reducer::UpdateError::Http});
            return;
        }
        gateway->fetch([this](const ManifestFetchResult& result) {
            if (!result.manifest.has_value()) {
                dispatch(reducer::update_event::CheckFailed{result.error});
                return;
            }
            manifestBody_ = result.body;
            dispatch(reducer::update_event::ManifestArrived{*result.manifest});
        });
        return;
    }

    if (const auto* schedule = std::get_if<reducer::update_effect::ScheduleNextCheck>(&effect)) {
        if (!status_.value().checksEnabled) { return; }
        int delay = schedule->delayMs;
        // Jitter the failure ladder only: the 15 s startup delay and the 4 h
        // interval are asserted verbatim, and spreading retries is what the
        // jitter is for.
        if (status_.value().phase == reducer::UpdatePhase::Failed) {
            const double unit = QRandomGenerator::global()->generateDouble();
            delay = reducer::jitteredDelayMs(delay, unit);
        }
        pendingCheckDelayMs_ = delay;
        checkTimer_->start(delay);
        return;
    }

    if (const auto* download = std::get_if<reducer::update_effect::StartDownload>(&effect)) {
        const reducer::update_effect::StartDownload request = *download;
        runOnWorker([this, request] {
            auto* staging = stagingStore();
            if (staging == nullptr || staging->root().isEmpty()) {
                runOnMain([this] {
                    dispatch(reducer::update_event::DownloadFailed{reducer::UpdateError::Io});
                });
                return;
            }
            if (!staging->hasRoomFor(static_cast<qint64>(request.size))) {
                runOnMain([this] {
                    dispatch(reducer::update_event::DownloadFailed{reducer::UpdateError::DiskFull});
                });
                return;
            }
            auto* gateway = downloadGateway();
            if (gateway == nullptr) {
                runOnMain([this] {
                    dispatch(reducer::update_event::DownloadFailed{reducer::UpdateError::Io});
                });
                return;
            }
            DownloadRequest job;
            job.url = request.url;
            job.sha256 = request.sha256;
            job.size = static_cast<qint64>(request.size);
            job.version = request.version;
            job.partPath = staging->partPathFor(request.version);
            gateway->start(
                job,
                [this](qint64 total) {
                    runOnMain([this, total] {
                        dispatch(
                            reducer::update_event::DownloadStarted{static_cast<quint64>(total)});
                    });
                },
                [this](qint64 receivedBytes) {
                    runOnMain([this, receivedBytes] {
                        dispatch(reducer::update_event::DownloadProgress{
                            static_cast<quint64>(receivedBytes)});
                    });
                },
                [this](const DownloadOutcome& outcome) {
                    runOnMain([this, outcome] {
                        if (outcome.ok) {
                            dispatch(reducer::update_event::DownloadFinished{outcome.partPath});
                        } else {
                            dispatch(reducer::update_event::DownloadFailed{outcome.error});
                        }
                    });
                });
        });
        return;
    }

    if (std::get_if<reducer::update_effect::AbortDownload>(&effect) != nullptr) {
        runOnWorker([this] {
            // Deliberately NOT downloadGateway(): an abort with nothing in
            // flight (checks turned off before any download started) must not
            // be the thing that finally constructs a QNAM.
            DownloadGateway* gateway =
                ports_.download ? ports_.download.get() : downloadOwned_.get();
            if (gateway != nullptr) { gateway->abort(); }
        });
        return;
    }

    if (const auto* verify = std::get_if<reducer::update_effect::VerifyAndPromote>(&effect)) {
        const QString version = verify->version;
        const QString sha256 = verify->sha256;
        const qint64 size = static_cast<qint64>(status_.value().availableAsset.size);
        const QByteArray body = manifestBody_;
        runOnWorker([this, version, sha256, size, body] {
            auto* staging = stagingStore();
            const auto readyDir = staging != nullptr ? staging->promote(version, sha256, size, body)
                                                     : std::optional<QString>{};
            runOnMain([this, readyDir] {
                if (readyDir.has_value()) {
                    dispatch(reducer::update_event::VerifyOk{*readyDir});
                } else {
                    dispatch(reducer::update_event::VerifyFailed{});
                }
            });
        });
        return;
    }

    if (const auto* discard = std::get_if<reducer::update_effect::DiscardStaged>(&effect)) {
        const QString version = discard->version;
        runOnWorker([this, version] {
            if (auto* staging = stagingStore()) { staging->discard(version); }
        });
        return;
    }

    if (std::get_if<reducer::update_effect::SweepStaging>(&effect) != nullptr) {
        const QString current = status_.value().currentVersion;
        runOnWorker([this, current] {
            if (auto* staging = stagingStore()) { staging->sweep(current); }
        });
        return;
    }

    if (std::get_if<reducer::update_effect::PersistLastCheck>(&effect) != nullptr) {
        settings_.setValue(QLatin1String(source::kKeyUpdatesLastCheckUtcMs), nowMs());
        settings_.sync();
        return;
    }

    if (const auto* notifyEffect = std::get_if<reducer::update_effect::Notify>(&effect)) {
        const QString key = noticeKey(notifyEffect->notice, notifyEffect->version);
        if (firedNotices_.contains(key)) { return; }
        firedNotices_.append(key);
        emit notice(notifyEffect->notice, notifyEffect->version);
        return;
    }
}

// ── Ports and threading ─────────────────────────────────────────────────────

ManifestGateway* UpdateCoordinator::manifestGateway() {
    if (ports_.manifest) { return ports_.manifest.get(); }
    // Constructed on first use, which is what keeps a Disabled client free of
    // any QNAM at all.
    if (!manifestOwned_) { manifestOwned_ = std::make_unique<HttpManifestGateway>(this); }
    return manifestOwned_.get();
}

StagingStore* UpdateCoordinator::stagingStore() {
    return ports_.staging ? ports_.staging.get() : stagingOwned_.get();
}

DownloadGateway* UpdateCoordinator::downloadGateway() {
    if (ports_.download) { return ports_.download.get(); }
    if (!downloadOwned_) {
        // On the worker: a QNetworkAccessManager belongs to the thread that
        // created it. Parentless, because the coordinator lives elsewhere.
        downloadOwned_ = std::make_unique<HttpDownloadGateway>();
    }
    return downloadOwned_.get();
}

void UpdateCoordinator::runOnWorker(std::function<void()> job) {
    if (!threaded()) {
        job();
        return;
    }
    QMetaObject::invokeMethod(workerContext_, std::move(job), Qt::QueuedConnection);
}

void UpdateCoordinator::runOnMain(std::function<void()> job) {
    if (!threaded()) {
        job();
        return;
    }
    QMetaObject::invokeMethod(this, std::move(job), Qt::QueuedConnection);
}

} // namespace dish::update
