// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// One reducer arm per event, with the phase guards inside it: the updater's
// interesting rules are about WHAT arrived, not where it arrived. The tests pin
// the effect lists in order, so a reordering is a behaviour change, not a
// cleanup.

#include "core/reducer/UpdateMachine.h"

#include "core/update/UpdateVersion.h"

namespace dish::reducer {

namespace {

template <class T> const T* as(const UpdateEvent& e) { return std::get_if<T>(&e); }

UpdateReduction stay(UpdateStatus s) { return UpdateReduction{std::move(s), {}}; }

// A stage that is genuinely applicable right now: present, parsable, and newer
// than the running build. Anything else is sweepable junk.
bool hasApplicableStage(const UpdateStatus& s) {
    return !s.stagedVersion.isEmpty() &&
           dish::update::isStrictlyNewer(s.stagedVersion, s.currentVersion);
}

// The ONE keep rule for a staged build (see the header): it must be exactly the
// version the newest manifest offers, and that version must beat this build.
// Every other combination (yank, supersede, already-applied) is discarded, and
// that is what makes a post-apply loop unreachable.
bool stageSurvivesManifest(const UpdateStatus& s, const QString& manifestVersion) {
    if (s.stagedVersion.isEmpty()) { return false; }
    return s.stagedVersion == manifestVersion &&
           dish::update::isStrictlyNewer(manifestVersion, s.currentVersion);
}

// A failed check or download must never erase a Ready stage: those bytes are
// already verified and still apply at the next start. The failure still counts
// toward the backoff ladder, but `error` always describes the phase the status
// actually settles in, so a Ready pill never carries a stale error token.
UpdateReduction settleFailure(const UpdateStatus& s, UpdateError error,
                              std::vector<UpdateEffect> before) {
    UpdateStatus n = s;
    n.consecutiveFailures = s.consecutiveFailures + 1;
    if (hasApplicableStage(s)) {
        n.phase = UpdatePhase::Ready;
        n.error = UpdateError::None;
        n.receivedBytes = n.totalBytes;
    } else {
        n.phase = UpdatePhase::Failed;
        n.error = error;
        n.receivedBytes = 0;
    }
    n.meteredDeferred = false;
    std::vector<UpdateEffect> fx = std::move(before);
    fx.push_back(update_effect::ScheduleNextCheck{backoffDelayMs(n.consecutiveFailures)});
    return UpdateReduction{std::move(n), std::move(fx)};
}

// Everything the manifest says, minus the phase decision.
UpdateStatus applyManifestFacts(const UpdateStatus& s, const dish::update::UpdateManifest& m) {
    UpdateStatus n = s;
    n.minimumSupportedVersion = m.minimumSupportedVersion;
    n.required = dish::update::isStrictlyNewer(m.minimumSupportedVersion, s.currentVersion);
    n.consecutiveFailures = 0;
    n.error = UpdateError::None;
    return n;
}

} // namespace

static UpdateReduction onPrefsChanged(const UpdateStatus& s, const update_event::PrefsChanged& e) {
    UpdateStatus n = s;
    n.checksEnabled = e.checksEnabled;
    n.autoDownload = e.autoDownload;
    n.skippedVersion = e.skippedVersion;

    if (!e.checksEnabled) {
        // Off means off from EVERY phase: no timer is re-armed and the
        // coordinator constructs no QNAM while Disabled.
        std::vector<UpdateEffect> fx;
        if (s.phase == UpdatePhase::Downloading) { fx.push_back(update_effect::AbortDownload{}); }
        n.phase = UpdatePhase::Disabled;
        n.error = UpdateError::None;
        n.receivedBytes = 0;
        n.meteredDeferred = false;
        return UpdateReduction{std::move(n), std::move(fx)};
    }
    if (s.phase == UpdatePhase::Disabled) {
        // Re-enabling behaves exactly like a cold start.
        n.phase = UpdatePhase::Idle;
        return UpdateReduction{
            std::move(n),
            {update_effect::SweepStaging{}, update_effect::ScheduleNextCheck{kStartupDelayMs}}};
    }
    // Turning auto-download on while an update is merely announced starts
    // it now rather than at the next 4 h tick: the toggle is the consent.
    const bool canStart = e.autoDownload && !s.metered && !s.portable &&
                          s.phase == UpdatePhase::Available && !s.availableAsset.url.isEmpty() &&
                          s.availableVersion != e.skippedVersion;
    if (canStart) {
        n.phase = UpdatePhase::Downloading;
        n.meteredDeferred = false;
        n.receivedBytes = 0;
        return UpdateReduction{
            std::move(n),
            {update_effect::StartDownload{s.availableAsset.url, s.availableAsset.sha256,
                                          static_cast<quint64>(s.availableAsset.size),
                                          s.availableVersion}}};
    }
    return stay(std::move(n));
}

// ── Check lifecycle ─────────────────────────────────────────────────────
static UpdateReduction onCheckRequested(const UpdateStatus& s,
                                        const update_event::CheckRequested& e) {
    if (s.phase == UpdatePhase::Disabled || s.phase == UpdatePhase::Checking) { return stay(s); }
    if (s.phase == UpdatePhase::Downloading || s.phase == UpdatePhase::Verifying) {
        // Re-arm rather than drop the cadence: a 40 MB download on a slow
        // link can outlive a periodic tick.
        return UpdateReduction{s, {update_effect::ScheduleNextCheck{kPeriodicIntervalMs}}};
    }
    if (!s.online) {
        // The reachability gate answers WITHOUT touching the network, which
        // is the whole point: a captive portal never gets a request.
        return settleFailure(s, UpdateError::Offline, {});
    }
    UpdateStatus n = s;
    n.phase = UpdatePhase::Checking;
    return UpdateReduction{std::move(n),
                           {update_effect::SweepStaging{},
                            update_effect::FetchManifest{e.trigger == UpdateTrigger::Manual}}};
}

static UpdateReduction onManifestArrived(const UpdateStatus& s,
                                         const update_event::ManifestArrived& e) {
    if (s.phase != UpdatePhase::Checking) { return stay(s); }
    const dish::update::UpdateManifest& m = e.manifest;
    UpdateStatus n = applyManifestFacts(s, m);

    std::vector<UpdateEffect> fx{update_effect::PersistLastCheck{}};
    if (!s.stagedVersion.isEmpty() && !stageSurvivesManifest(s, m.version)) {
        fx.push_back(update_effect::DiscardStaged{s.stagedVersion});
        n.stagedVersion.clear();
    }

    const bool newer = dish::update::isStrictlyNewer(m.version, s.currentVersion);
    const bool skipped = !s.skippedVersion.isEmpty() && s.skippedVersion == m.version;

    if (!newer || (skipped && !n.required)) {
        // Nothing to offer, or the user muted this exact version. A skip is
        // ignored while the running build is below the supported minimum.
        n.phase = UpdatePhase::UpToDate;
        n.availableVersion.clear();
        n.notesUrl.clear();
        n.availableAsset = {};
        n.totalBytes = 0;
        n.receivedBytes = 0;
        n.meteredDeferred = false;
        fx.push_back(update_effect::ScheduleNextCheck{kPeriodicIntervalMs});
        return UpdateReduction{std::move(n), std::move(fx)};
    }

    n.availableVersion = m.version;
    n.notesUrl = m.releaseNotesUrl;
    n.availableAsset = m.setupAsset;
    n.totalBytes = static_cast<quint64>(m.setupAsset.size);
    n.meteredDeferred = false;

    if (n.required) { fx.push_back(update_effect::Notify{UpdateNotice::Unsupported, m.version}); }

    if (n.stagedVersion == m.version) {
        // Already staged and still the newest: nothing to fetch.
        n.phase = UpdatePhase::Ready;
        n.receivedBytes = n.totalBytes;
        fx.push_back(update_effect::Notify{UpdateNotice::Ready, m.version});
        fx.push_back(update_effect::ScheduleNextCheck{kPeriodicIntervalMs});
        return UpdateReduction{std::move(n), std::move(fx)};
    }

    if (s.autoDownload && !s.metered && !s.portable) {
        n.phase = UpdatePhase::Downloading;
        n.receivedBytes = 0;
        fx.push_back(update_effect::StartDownload{m.setupAsset.url, m.setupAsset.sha256,
                                                  static_cast<quint64>(m.setupAsset.size),
                                                  m.version});
        fx.push_back(update_effect::ScheduleNextCheck{kPeriodicIntervalMs});
        return UpdateReduction{std::move(n), std::move(fx)};
    }

    // Notify-only: auto-download off, a metered link, or a portable copy
    // that must never apply anything.
    n.phase = UpdatePhase::Available;
    n.receivedBytes = 0;
    n.meteredDeferred = s.metered && s.autoDownload && !s.portable;
    fx.push_back(update_effect::Notify{UpdateNotice::Available, m.version});
    fx.push_back(update_effect::ScheduleNextCheck{kPeriodicIntervalMs});
    return UpdateReduction{std::move(n), std::move(fx)};
}

static UpdateReduction onCheckFailed(const UpdateStatus& s, const update_event::CheckFailed& e) {
    if (s.phase != UpdatePhase::Checking) { return stay(s); }
    return settleFailure(s, e.error, {});
}

// ── Download lifecycle ──────────────────────────────────────────────────
static UpdateReduction onDownloadRequested(const UpdateStatus& s) {
    // Manual download: allowed on a metered link (the user asked), never on
    // a portable copy (there is nothing to apply it to).
    if (s.phase != UpdatePhase::Available || s.portable || s.availableAsset.url.isEmpty()) {
        return stay(s);
    }
    UpdateStatus n = s;
    n.phase = UpdatePhase::Downloading;
    n.receivedBytes = 0;
    n.meteredDeferred = false;
    n.error = UpdateError::None;
    return UpdateReduction{std::move(n),
                           {update_effect::StartDownload{
                               s.availableAsset.url, s.availableAsset.sha256,
                               static_cast<quint64>(s.availableAsset.size), s.availableVersion}}};
}

static UpdateReduction onDownloadStarted(const UpdateStatus& s,
                                         const update_event::DownloadStarted& e) {
    if (s.phase != UpdatePhase::Downloading) { return stay(s); }
    UpdateStatus n = s;
    n.totalBytes = e.total;
    return stay(std::move(n));
}

static UpdateReduction onDownloadProgress(const UpdateStatus& s,
                                          const update_event::DownloadProgress& e) {
    if (s.phase != UpdatePhase::Downloading) { return stay(s); }
    UpdateStatus n = s;
    n.receivedBytes = e.received;
    return stay(std::move(n));
}

static UpdateReduction onDownloadFinished(const UpdateStatus& s) {
    if (s.phase != UpdatePhase::Downloading) { return stay(s); }
    UpdateStatus n = s;
    n.phase = UpdatePhase::Verifying;
    return UpdateReduction{
        std::move(n),
        {update_effect::VerifyAndPromote{s.availableVersion, s.availableAsset.sha256}}};
}

static UpdateReduction onDownloadFailed(const UpdateStatus& s,
                                        const update_event::DownloadFailed& e) {
    if (s.phase != UpdatePhase::Downloading) { return stay(s); }
    return settleFailure(s, e.error, {});
}

static UpdateReduction onVerifyOk(const UpdateStatus& s) {
    if (s.phase != UpdatePhase::Verifying) { return stay(s); }
    UpdateStatus n = s;
    n.phase = UpdatePhase::Ready;
    n.stagedVersion = s.availableVersion;
    n.receivedBytes = s.totalBytes;
    n.error = UpdateError::None;
    n.consecutiveFailures = 0;
    return UpdateReduction{std::move(n),
                           {update_effect::Notify{UpdateNotice::Ready, s.availableVersion},
                            update_effect::SweepStaging{}}};
}

static UpdateReduction onVerifyFailed(const UpdateStatus& s) {
    if (s.phase != UpdatePhase::Verifying) { return stay(s); }
    // The bytes on disk disagree with the manifest: drop them entirely and
    // restart from zero next cycle. No resume, nothing partial kept.
    UpdateStatus n = s;
    n.stagedVersion.clear();
    return settleFailure(n, UpdateError::Corrupt,
                         {update_effect::DiscardStaged{s.availableVersion}});
}

// ── Staging discovered out of band ──────────────────────────────────────
static UpdateReduction onStagedFound(const UpdateStatus& s, const update_event::StagedFound& e) {
    UpdateStatus n = s;
    const bool usable =
        dish::update::isStrictlyNewer(e.version, s.currentVersion) && e.version != s.skippedVersion;
    if (!usable) {
        n.stagedVersion.clear();
        return UpdateReduction{std::move(n), {update_effect::DiscardStaged{e.version}}};
    }
    n.stagedVersion = e.version;
    if (s.phase == UpdatePhase::Idle || s.phase == UpdatePhase::UpToDate ||
        s.phase == UpdatePhase::Failed) {
        n.phase = UpdatePhase::Ready;
        n.availableVersion = e.version;
        n.error = UpdateError::None;
        return UpdateReduction{std::move(n),
                               {update_effect::Notify{UpdateNotice::Ready, e.version}}};
    }
    // Disabled keeps its phase (the boot gate still applies what is staged)
    // and an in-flight check/download just records what exists.
    return stay(std::move(n));
}

static UpdateReduction onStagedInvalidated(const UpdateStatus& s) {
    if (s.stagedVersion.isEmpty()) { return stay(s); }
    UpdateStatus n = s;
    n.stagedVersion.clear();
    if (s.phase == UpdatePhase::Ready) {
        n.phase = UpdatePhase::Idle;
        n.receivedBytes = 0;
    }
    return stay(std::move(n));
}

// ── User decisions ──────────────────────────────────────────────────────
static UpdateReduction onSkipRequested(const UpdateStatus& s,
                                       const update_event::SkipRequested& e) {
    // A required update cannot be skipped: the build is already unsupported.
    if (s.required || e.version.isEmpty()) { return stay(s); }
    UpdateStatus n = s;
    n.skippedVersion = e.version;
    std::vector<UpdateEffect> fx;
    if (s.phase == UpdatePhase::Downloading) { fx.push_back(update_effect::AbortDownload{}); }
    if (s.stagedVersion == e.version && !s.stagedVersion.isEmpty()) {
        fx.push_back(update_effect::DiscardStaged{s.stagedVersion});
        n.stagedVersion.clear();
    }
    if (s.availableVersion == e.version) {
        n.phase = UpdatePhase::UpToDate;
        n.availableVersion.clear();
        n.notesUrl.clear();
        n.availableAsset = {};
        n.receivedBytes = 0;
        n.totalBytes = 0;
        n.meteredDeferred = false;
    }
    return UpdateReduction{std::move(n), std::move(fx)};
}

// ── Connectivity ────────────────────────────────────────────────────────
static UpdateReduction onMeteredChanged(const UpdateStatus& s,
                                        const update_event::MeteredChanged& e) {
    UpdateStatus n = s;
    n.metered = e.metered;
    const bool resumes = !e.metered && s.meteredDeferred && s.phase == UpdatePhase::Available &&
                         s.autoDownload && !s.portable && !s.availableAsset.url.isEmpty();
    if (!resumes) {
        // A link that turns metered mid-download is left alone: aborting
        // would throw away bytes already paid for.
        n.meteredDeferred = s.meteredDeferred && e.metered;
        return stay(std::move(n));
    }
    n.phase = UpdatePhase::Downloading;
    n.meteredDeferred = false;
    n.receivedBytes = 0;
    return UpdateReduction{std::move(n),
                           {update_effect::StartDownload{
                               s.availableAsset.url, s.availableAsset.sha256,
                               static_cast<quint64>(s.availableAsset.size), s.availableVersion}}};
}

static UpdateReduction onReachabilityChanged(const UpdateStatus& s,
                                             const update_event::ReachabilityChanged& e) {
    UpdateStatus n = s;
    n.online = e.online;
    const bool wasOfflineGated =
        s.phase == UpdatePhase::Failed && s.error == UpdateError::Offline && s.checksEnabled;
    if (e.online && wasOfflineGated) {
        return UpdateReduction{std::move(n),
                               {update_effect::ScheduleNextCheck{kReconnectCheckDelayMs}}};
    }
    return stay(std::move(n));
}

UpdateReduction reduceUpdate(const UpdateStatus& s, const UpdateEvent& event) {
    if (const auto* e = as<update_event::PrefsChanged>(event)) { return onPrefsChanged(s, *e); }
    if (const auto* e = as<update_event::CheckRequested>(event)) { return onCheckRequested(s, *e); }
    if (const auto* e = as<update_event::ManifestArrived>(event)) {
        return onManifestArrived(s, *e);
    }
    if (const auto* e = as<update_event::CheckFailed>(event)) { return onCheckFailed(s, *e); }
    if (as<update_event::DownloadRequested>(event) != nullptr) { return onDownloadRequested(s); }
    if (const auto* e = as<update_event::DownloadStarted>(event)) {
        return onDownloadStarted(s, *e);
    }
    if (const auto* e = as<update_event::DownloadProgress>(event)) {
        return onDownloadProgress(s, *e);
    }
    if (as<update_event::DownloadFinished>(event) != nullptr) { return onDownloadFinished(s); }
    if (const auto* e = as<update_event::DownloadFailed>(event)) { return onDownloadFailed(s, *e); }
    if (as<update_event::VerifyOk>(event) != nullptr) { return onVerifyOk(s); }
    if (as<update_event::VerifyFailed>(event) != nullptr) { return onVerifyFailed(s); }
    if (const auto* e = as<update_event::StagedFound>(event)) { return onStagedFound(s, *e); }
    if (as<update_event::StagedInvalidated>(event) != nullptr) { return onStagedInvalidated(s); }
    if (const auto* e = as<update_event::SkipRequested>(event)) { return onSkipRequested(s, *e); }
    if (const auto* e = as<update_event::MeteredChanged>(event)) { return onMeteredChanged(s, *e); }
    if (const auto* e = as<update_event::ReachabilityChanged>(event)) {
        return onReachabilityChanged(s, *e);
    }

    return stay(s);
}

} // namespace dish::reducer
