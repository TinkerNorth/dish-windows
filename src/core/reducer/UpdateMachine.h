// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The auto-updater's lifecycle FSM. Same shape as UsbPathMachine: every
// (phase x event) pair is total, `reduceUpdate` does no IO and reads no clock,
// and effects come back as data for the coordinator (src/update/
// UpdateCoordinator) to execute against the network, the disk and the timers.
//
// Two rules earn their own comment because getting either wrong ships a client
// that updates forever or never:
//   - Ordering is by the parsed version triple ONLY. `publishedAt` is display
//     text and no wall clock enters this file, so a machine with a skewed clock
//     behaves exactly like one without.
//   - A stage is kept only when it IS the version the newest manifest offers
//     and that version is newer than this build. Anything else (a yanked
//     release, a superseded stage, a stage at or below the running version) is
//     discarded, which is what makes the post-apply loop impossible.
//
// The scheduling constants live here rather than in core/reducer/Backoff.h on
// purpose: that ladder is the 1 s..60 s satellite reconnect scale, and polling
// GitHub on it would be abuse.

#pragma once

#include "core/update/UpdateManifest.h"

#include <QString>

#include <variant>
#include <vector>

namespace dish::reducer {

enum class UpdatePhase {
    Disabled,    // checks turned off: no timers, no QNAM, no network IO at all
    Idle,        // enabled, nothing known yet
    Checking,    // a manifest fetch is in flight
    UpToDate,    // the newest published release is this one (or a skipped one)
    Available,   // newer exists, not being downloaded (notify-only / metered / portable)
    Downloading, // the setup asset is streaming to the .part file
    Verifying,   // downloaded; re-hashing from disk before the promote
    Ready,       // verified bytes are staged and will apply at the next start
    Failed,      // the last attempt failed; `error` says how
};

enum class UpdateError {
    None,
    Offline,         // reachability gate; no request was made
    Http,            // transport, TLS, status code, 404 in a publish window
    ManifestInvalid, // any UpdateManifest::parse rejection, incl. portal HTML
    Corrupt,         // the on-disk re-hash disagreed with the manifest
    DiskFull,        // preflight or a mid-write ENOSPC
    Io,              // staging read/write/rename failed
    Stalled,         // no bytes for 60 s, or an overrun past the declared size
    ApplyFailed,     // the staged installer was spawned twice and never landed
};

// The coordinator turns these into localized toasts; the engine never vends a
// sentence.
enum class UpdateNotice { Ready, Available, Unsupported, Updated };

enum class UpdateTrigger { Startup, Periodic, Manual, Retry };

// ==-comparable so the coordinator's Observable suppresses no-op re-emits.
struct UpdateStatus {
    UpdatePhase phase = UpdatePhase::Idle;
    QString currentVersion; // DISH_VERSION, seeded once by the coordinator
    QString availableVersion;
    QString notesUrl;
    QString minimumSupportedVersion;
    QString stagedVersion; // the ready\<v> on disk, "" when none

    quint64 receivedBytes = 0;
    quint64 totalBytes = 0;
    int consecutiveFailures = 0;
    UpdateError error = UpdateError::None;

    bool checksEnabled = true;
    bool autoDownload = true;
    bool required = false;        // currentVersion < minimumSupportedVersion
    bool meteredDeferred = false; // an auto-download is waiting for an unmetered link
    bool portable = false;        // no uninstall.exe sibling: notify only, never apply

    // Beyond the surfaced state, and all four are what make `reduceUpdate`
    // total without a second source of truth:
    //   `availableAsset` because DownloadRequested carries no payload and a
    //   manual download can happen long after the manifest arrived;
    //   `skippedVersion` because a skip must suppress restaging while the
    //   manifest still offers that exact version;
    //   `metered` / `online` because the gates are evaluated at CheckRequested
    //   and DownloadRequested time, not when the connectivity event arrived.
    dish::update::UpdateAsset availableAsset;
    QString skippedVersion;
    bool metered = false;
    bool online = true;

    bool operator==(const UpdateStatus& o) const {
        return phase == o.phase && currentVersion == o.currentVersion &&
               availableVersion == o.availableVersion && notesUrl == o.notesUrl &&
               minimumSupportedVersion == o.minimumSupportedVersion &&
               stagedVersion == o.stagedVersion && receivedBytes == o.receivedBytes &&
               totalBytes == o.totalBytes && consecutiveFailures == o.consecutiveFailures &&
               error == o.error && checksEnabled == o.checksEnabled &&
               autoDownload == o.autoDownload && required == o.required &&
               meteredDeferred == o.meteredDeferred && portable == o.portable &&
               availableAsset == o.availableAsset && skippedVersion == o.skippedVersion &&
               metered == o.metered && online == o.online;
    }
    bool operator!=(const UpdateStatus& o) const { return !(*this == o); }
};

// ── Scheduling constants ────────────────────────────────────────────────────
// Startup is delayed so the check never competes with the first frame; the min
// gap keeps a relaunch loop from hammering the permalink, and the future-jump
// escape means a clock that reads 2099 costs one extra check, not silence.

inline constexpr int kStartupDelayMs = 15'000;
inline constexpr qint64 kMinCheckGapMs = 60LL * 60 * 1000;            // 1 h
inline constexpr qint64 kFutureSkewEscapeMs = 24LL * 60 * 60 * 1000;  // 24 h
inline constexpr int kPeriodicIntervalMs = 4 * 60 * 60 * 1000;        // 4 h
inline constexpr int kBackoffBaseMs = 10 * 60 * 1000;                 // 10 min
inline constexpr int kBackoffCapMs = 6 * 60 * 60 * 1000;              // 6 h
inline constexpr double kBackoffJitter = 0.2;                         // +-20 percent
inline constexpr int kManualMinGapMs = 10'000;                        // manual rate limit
inline constexpr int kReconnectCheckDelayMs = 30'000;                 // after Online returns
inline constexpr qint64 kDownloadHeadroomBytes = 200LL * 1024 * 1024; // free-space preflight
inline constexpr int kStallTimeoutMs = 60'000;                        // no bytes for 60 s
inline constexpr qint64 kOverrunAllowanceBytes = 1LL * 1024 * 1024;   // declared size + 1 MiB
inline constexpr qint64 kStagingPartMaxAgeMs = 24LL * 60 * 60 * 1000; // .part sweep age
inline constexpr int kMaxApplyAttemptsPerVersion = 2;                 // then quarantine

// 10, 20, 40 ... 360 min. `failures` is the count INCLUDING the one just
// recorded, so the first failure waits kBackoffBaseMs.
constexpr int backoffDelayMs(int failures) {
    if (failures <= 1) { return kBackoffBaseMs; }
    long long delay = kBackoffBaseMs;
    for (int i = 1; i < failures; ++i) {
        delay *= 2;
        if (delay >= kBackoffCapMs) { return kBackoffCapMs; }
    }
    return static_cast<int>(delay);
}

// Deterministic given `unit` in [0, 1]: the coordinator supplies the random
// draw, so the ladder itself stays pinnable in a test. Applied ONLY to failure
// backoff (spreading a fleet's retries), never to the 15 s startup delay or
// the 4 h interval, both of which are asserted verbatim.
constexpr int jitteredDelayMs(int baseMs, double unit) {
    const double clamped = unit < 0.0 ? 0.0 : (unit > 1.0 ? 1.0 : unit);
    const double factor = 1.0 - kBackoffJitter + (2.0 * kBackoffJitter * clamped);
    const double scaled = static_cast<double>(baseMs) * factor;
    return scaled < 0.0 ? 0 : static_cast<int>(scaled);
}

// ── Events ──────────────────────────────────────────────────────────────────

namespace update_event {

// The reactive preference slice; the coordinator republishes it on every store
// change, so this event is also how "checks off" reaches every phase.
struct PrefsChanged {
    bool checksEnabled = true;
    bool autoDownload = true;
    QString skippedVersion;
    bool operator==(const PrefsChanged& o) const {
        return checksEnabled == o.checksEnabled && autoDownload == o.autoDownload &&
               skippedVersion == o.skippedVersion;
    }
};
struct CheckRequested {
    UpdateTrigger trigger = UpdateTrigger::Periodic;
    bool operator==(const CheckRequested& o) const { return trigger == o.trigger; }
};
struct ManifestArrived {
    dish::update::UpdateManifest manifest;
    bool operator==(const ManifestArrived& o) const { return manifest == o.manifest; }
};
struct CheckFailed {
    UpdateError error = UpdateError::Http;
    bool operator==(const CheckFailed& o) const { return error == o.error; }
};
struct DownloadRequested {
    bool operator==(const DownloadRequested&) const { return true; }
};
struct DownloadStarted {
    quint64 total = 0;
    bool operator==(const DownloadStarted& o) const { return total == o.total; }
};
struct DownloadProgress {
    quint64 received = 0;
    bool operator==(const DownloadProgress& o) const { return received == o.received; }
};
struct DownloadFinished {
    QString partPath;
    bool operator==(const DownloadFinished& o) const { return partPath == o.partPath; }
};
struct DownloadFailed {
    UpdateError error = UpdateError::Http;
    bool operator==(const DownloadFailed& o) const { return error == o.error; }
};
struct VerifyOk {
    QString readyDir;
    bool operator==(const VerifyOk& o) const { return readyDir == o.readyDir; }
};
struct VerifyFailed {
    bool operator==(const VerifyFailed&) const { return true; }
};
// The startup scan (and the post-promote rescan) reporting what survived the
// janitor.
struct StagedFound {
    QString version;
    QString dir;
    bool operator==(const StagedFound& o) const { return version == o.version && dir == o.dir; }
};
struct StagedInvalidated {
    bool operator==(const StagedInvalidated&) const { return true; }
};
struct SkipRequested {
    QString version;
    bool operator==(const SkipRequested& o) const { return version == o.version; }
};
struct MeteredChanged {
    bool metered = false;
    bool operator==(const MeteredChanged& o) const { return metered == o.metered; }
};
struct ReachabilityChanged {
    bool online = true;
    bool operator==(const ReachabilityChanged& o) const { return online == o.online; }
};

} // namespace update_event

using UpdateEvent = std::variant<
    update_event::PrefsChanged, update_event::CheckRequested, update_event::ManifestArrived,
    update_event::CheckFailed, update_event::DownloadRequested, update_event::DownloadStarted,
    update_event::DownloadProgress, update_event::DownloadFinished, update_event::DownloadFailed,
    update_event::VerifyOk, update_event::VerifyFailed, update_event::StagedFound,
    update_event::StagedInvalidated, update_event::SkipRequested, update_event::MeteredChanged,
    update_event::ReachabilityChanged>;

// ── Effects (returned as data; executed by the coordinator) ─────────────────

namespace update_effect {

// GET the release permalink. `manual` only relaxes the coordinator's rate
// limiting; the request itself is identical.
struct FetchManifest {
    bool manual = false;
    bool operator==(const FetchManifest& o) const { return manual == o.manual; }
};
// Re-arm the single check timer. Failure delays are the UNJITTERED ladder
// value; the coordinator jitters those and only those.
struct ScheduleNextCheck {
    int delayMs = 0;
    bool operator==(const ScheduleNextCheck& o) const { return delayMs == o.delayMs; }
};
struct StartDownload {
    QString url;
    QString sha256;
    quint64 size = 0;
    QString version;
    bool operator==(const StartDownload& o) const {
        return url == o.url && sha256 == o.sha256 && size == o.size && version == o.version;
    }
};
struct AbortDownload {
    bool operator==(const AbortDownload&) const { return true; }
};
// Re-read the .part from disk, hash it whole, and publish ready\<version> with
// the marker written LAST. Never trusts the hash the stream computed.
struct VerifyAndPromote {
    QString version;
    QString sha256;
    bool operator==(const VerifyAndPromote& o) const {
        return version == o.version && sha256 == o.sha256;
    }
};
struct DiscardStaged {
    QString version;
    bool operator==(const DiscardStaged& o) const { return version == o.version; }
};
struct SweepStaging {
    bool operator==(const SweepStaging&) const { return true; }
};
struct PersistLastCheck {
    bool operator==(const PersistLastCheck&) const { return true; }
};
struct Notify {
    UpdateNotice notice = UpdateNotice::Available;
    QString version;
    bool operator==(const Notify& o) const { return notice == o.notice && version == o.version; }
};

} // namespace update_effect

using UpdateEffect = std::variant<
    update_effect::FetchManifest, update_effect::ScheduleNextCheck, update_effect::StartDownload,
    update_effect::AbortDownload, update_effect::VerifyAndPromote, update_effect::DiscardStaged,
    update_effect::SweepStaging, update_effect::PersistLastCheck, update_effect::Notify>;

struct UpdateReduction {
    UpdateStatus next;
    std::vector<UpdateEffect> effects;
};

UpdateReduction reduceUpdate(const UpdateStatus& s, const UpdateEvent& event);

} // namespace dish::reducer
