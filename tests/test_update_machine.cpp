// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The updater reducer. Effect lists are asserted as ORDERED vectors because the
// coordinator executes them in sequence.
//
// Three rules get the most cases, because getting any of them wrong ships a
// client that updates forever or never: ordering is by the parsed triple only
// (no wall clock anywhere), a stage survives only when it IS the version the
// newest manifest offers AND that version beats this build (the yank rule), and
// "checks off" means off from every phase with no timer re-armed.

#include "core/reducer/UpdateMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <QString>

#include <vector>

using dish::reducer::backoffDelayMs;
using dish::reducer::jitteredDelayMs;
using dish::reducer::kBackoffBaseMs;
using dish::reducer::kBackoffCapMs;
using dish::reducer::kDownloadHeadroomBytes;
using dish::reducer::kFutureSkewEscapeMs;
using dish::reducer::kManualMinGapMs;
using dish::reducer::kMaxApplyAttemptsPerVersion;
using dish::reducer::kMinCheckGapMs;
using dish::reducer::kOverrunAllowanceBytes;
using dish::reducer::kPeriodicIntervalMs;
using dish::reducer::kReconnectCheckDelayMs;
using dish::reducer::kStagingPartMaxAgeMs;
using dish::reducer::kStallTimeoutMs;
using dish::reducer::kStartupDelayMs;
using dish::reducer::reduceUpdate;
using dish::reducer::UpdateEffect;
using dish::reducer::UpdateError;
using dish::reducer::UpdateNotice;
using dish::reducer::UpdatePhase;
using dish::reducer::UpdateStatus;
using dish::reducer::UpdateTrigger;
using dish::update::UpdateAsset;
using dish::update::UpdateManifest;
namespace uev = dish::reducer::update_event;
namespace ufx = dish::reducer::update_effect;

namespace {

const QString kSha = QString(64, QLatin1Char('a'));

QString assetUrl(const QString& version) {
    return QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/download/v") +
           version + QStringLiteral("/dish-setup.exe");
}

UpdateManifest manifest(const QString& version, const QString& minimum = QStringLiteral("0.1.0"),
                        qint64 size = 41943040) {
    UpdateManifest m;
    m.schema = 1;
    m.product = QStringLiteral("dish-windows");
    m.version = version;
    m.channel = QStringLiteral("stable");
    m.publishedAt = QStringLiteral("2026-08-03T14:21:07Z");
    m.minimumSupportedVersion = minimum;
    m.releaseNotesUrl =
        QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/tag/v") + version;
    m.setupAsset.url = assetUrl(version);
    m.setupAsset.sha256 = kSha;
    m.setupAsset.size = size;
    return m;
}

UpdateAsset asset(const QString& version, qint64 size = 41943040) {
    UpdateAsset a;
    a.url = assetUrl(version);
    a.sha256 = kSha;
    a.size = size;
    return a;
}

UpdateStatus base(UpdatePhase phase = UpdatePhase::Idle) {
    UpdateStatus s;
    s.phase = phase;
    s.currentVersion = QStringLiteral("0.1.0");
    return s;
}

// An Available status carrying the asset a manual or resumed download reuses.
UpdateStatus available(const QString& version = QStringLiteral("0.2.0")) {
    UpdateStatus s = base(UpdatePhase::Available);
    s.availableVersion = version;
    s.availableAsset = asset(version);
    s.totalBytes = static_cast<quint64>(s.availableAsset.size);
    s.notesUrl =
        QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/tag/v") + version;
    return s;
}

} // namespace

// ── Scheduling constants ────────────────────────────────────────────────────

TEST_CASE("update machine: the schedule constants are the documented ones",
          "[update][update-fsm]") {
    CHECK(kStartupDelayMs == 15000);
    CHECK(kMinCheckGapMs == 60LL * 60 * 1000);
    CHECK(kFutureSkewEscapeMs == 24LL * 60 * 60 * 1000);
    CHECK(kPeriodicIntervalMs == 4 * 60 * 60 * 1000);
    CHECK(kBackoffBaseMs == 10 * 60 * 1000);
    CHECK(kBackoffCapMs == 6 * 60 * 60 * 1000);
    CHECK(kManualMinGapMs == 10000);
    CHECK(kReconnectCheckDelayMs == 30000);
    CHECK(kStallTimeoutMs == 60000);
    CHECK(kOverrunAllowanceBytes == 1LL * 1024 * 1024);
    CHECK(kDownloadHeadroomBytes == 200LL * 1024 * 1024);
    CHECK(kStagingPartMaxAgeMs == 24LL * 60 * 60 * 1000);
    CHECK(kMaxApplyAttemptsPerVersion == 2);
}

TEST_CASE("update machine: the backoff ladder doubles to a six-hour cap", "[update][update-fsm]") {
    CHECK(backoffDelayMs(0) == 10 * 60 * 1000);
    CHECK(backoffDelayMs(1) == 10 * 60 * 1000);
    CHECK(backoffDelayMs(2) == 20 * 60 * 1000);
    CHECK(backoffDelayMs(3) == 40 * 60 * 1000);
    CHECK(backoffDelayMs(4) == 80 * 60 * 1000);
    CHECK(backoffDelayMs(5) == 160 * 60 * 1000);
    CHECK(backoffDelayMs(6) == 320 * 60 * 1000);
    CHECK(backoffDelayMs(7) == 360 * 60 * 1000); // capped
    CHECK(backoffDelayMs(50) == kBackoffCapMs);
}

TEST_CASE("update machine: jitter stays inside plus or minus 20 percent", "[update][update-fsm]") {
    const int base = 10 * 60 * 1000;
    CHECK(jitteredDelayMs(base, 0.0) == static_cast<int>(base * 0.8));
    CHECK(jitteredDelayMs(base, 1.0) == static_cast<int>(base * 1.2));
    CHECK(jitteredDelayMs(base, 0.5) == base);
    // Out-of-range draws clamp rather than producing a negative delay.
    CHECK(jitteredDelayMs(base, -5.0) == static_cast<int>(base * 0.8));
    CHECK(jitteredDelayMs(base, 7.0) == static_cast<int>(base * 1.2));
    for (double unit = 0.0; unit <= 1.0; unit += 0.1) {
        const int delay = jitteredDelayMs(base, unit);
        CHECK(delay >= static_cast<int>(base * 0.8));
        CHECK(delay <= static_cast<int>(base * 1.2));
    }
}

// ── Preferences ─────────────────────────────────────────────────────────────

TEST_CASE("update machine: checks off means off from every phase", "[update][update-fsm]") {
    for (const UpdatePhase phase :
         {UpdatePhase::Idle, UpdatePhase::Checking, UpdatePhase::UpToDate, UpdatePhase::Available,
          UpdatePhase::Verifying, UpdatePhase::Ready, UpdatePhase::Failed}) {
        const auto r = reduceUpdate(base(phase), uev::PrefsChanged{false, true, QString()});
        CHECK(r.next.phase == UpdatePhase::Disabled);
        CHECK(r.next.error == UpdateError::None);
        CHECK_FALSE(r.next.checksEnabled);
        // No timer re-armed: a Disabled client constructs no QNAM at all.
        CHECK(r.effects.empty());
    }
}

TEST_CASE("update machine: turning checks off mid-download aborts the transfer",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Downloading;
    s.receivedBytes = 1024;
    const auto r = reduceUpdate(s, uev::PrefsChanged{false, true, QString()});
    CHECK(r.next.phase == UpdatePhase::Disabled);
    CHECK(r.next.receivedBytes == 0);
    const std::vector<UpdateEffect> expected{ufx::AbortDownload{}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: re-enabling checks behaves like a cold start", "[update][update-fsm]") {
    UpdateStatus s = base(UpdatePhase::Disabled);
    s.checksEnabled = false;
    const auto r = reduceUpdate(s, uev::PrefsChanged{true, true, QString()});
    CHECK(r.next.phase == UpdatePhase::Idle);
    const std::vector<UpdateEffect> expected{ufx::SweepStaging{},
                                             ufx::ScheduleNextCheck{kStartupDelayMs}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: turning auto-download on starts the announced download now",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.autoDownload = false;
    const auto r = reduceUpdate(s, uev::PrefsChanged{true, true, QString()});
    CHECK(r.next.phase == UpdatePhase::Downloading);
    CHECK(r.next.receivedBytes == 0);
    const std::vector<UpdateEffect> expected{ufx::StartDownload{
        assetUrl(QStringLiteral("0.2.0")), kSha, 41943040, QStringLiteral("0.2.0")}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: auto-download stays parked while metered, portable or skipped",
          "[update][update-fsm]") {
    SECTION("metered") {
        UpdateStatus s = available();
        s.autoDownload = false;
        s.metered = true;
        const auto r = reduceUpdate(s, uev::PrefsChanged{true, true, QString()});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.effects.empty());
    }
    SECTION("portable") {
        UpdateStatus s = available();
        s.autoDownload = false;
        s.portable = true;
        const auto r = reduceUpdate(s, uev::PrefsChanged{true, true, QString()});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.effects.empty());
    }
    SECTION("skipped") {
        UpdateStatus s = available();
        s.autoDownload = false;
        const auto r = reduceUpdate(s, uev::PrefsChanged{true, true, QStringLiteral("0.2.0")});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.next.skippedVersion == QStringLiteral("0.2.0"));
        CHECK(r.effects.empty());
    }
}

// ── Check lifecycle ─────────────────────────────────────────────────────────

TEST_CASE("update machine: a check sweeps first, then fetches", "[update][update-fsm]") {
    const auto r = reduceUpdate(base(), uev::CheckRequested{UpdateTrigger::Startup});
    CHECK(r.next.phase == UpdatePhase::Checking);
    const std::vector<UpdateEffect> expected{ufx::SweepStaging{}, ufx::FetchManifest{false}};
    CHECK(r.effects == expected);

    const auto manual = reduceUpdate(base(), uev::CheckRequested{UpdateTrigger::Manual});
    const std::vector<UpdateEffect> manualExpected{ufx::SweepStaging{}, ufx::FetchManifest{true}};
    CHECK(manual.effects == manualExpected);
}

TEST_CASE("update machine: a check while disabled or already checking does nothing",
          "[update][update-fsm]") {
    for (const UpdatePhase phase : {UpdatePhase::Disabled, UpdatePhase::Checking}) {
        const auto r = reduceUpdate(base(phase), uev::CheckRequested{UpdateTrigger::Periodic});
        CHECK(r.next.phase == phase);
        CHECK(r.effects.empty());
    }
}

TEST_CASE("update machine: a periodic tick during a transfer re-arms instead of interrupting",
          "[update][update-fsm]") {
    for (const UpdatePhase phase : {UpdatePhase::Downloading, UpdatePhase::Verifying}) {
        const auto r = reduceUpdate(base(phase), uev::CheckRequested{UpdateTrigger::Periodic});
        CHECK(r.next.phase == phase);
        const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: the offline gate answers without touching the network",
          "[update][update-fsm]") {
    UpdateStatus s = base();
    s.online = false;
    const auto r = reduceUpdate(s, uev::CheckRequested{UpdateTrigger::Periodic});
    CHECK(r.next.phase == UpdatePhase::Failed);
    CHECK(r.next.error == UpdateError::Offline);
    CHECK(r.next.consecutiveFailures == 1);
    // No FetchManifest: a captive portal never gets a request out of us.
    const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{backoffDelayMs(1)}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: a newer manifest with auto-download starts the transfer",
          "[update][update-fsm]") {
    const auto r = reduceUpdate(base(UpdatePhase::Checking),
                                uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
    CHECK(r.next.phase == UpdatePhase::Downloading);
    CHECK(r.next.availableVersion == QStringLiteral("0.2.0"));
    CHECK(r.next.totalBytes == 41943040u);
    CHECK(r.next.receivedBytes == 0);
    CHECK(r.next.consecutiveFailures == 0);
    CHECK(r.next.error == UpdateError::None);
    CHECK(r.next.notesUrl.endsWith(QStringLiteral("v0.2.0")));
    CHECK_FALSE(r.next.required);

    const std::vector<UpdateEffect> expected{ufx::PersistLastCheck{},
                                             ufx::StartDownload{assetUrl(QStringLiteral("0.2.0")),
                                                                kSha, 41943040,
                                                                QStringLiteral("0.2.0")},
                                             ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: notify-only modes announce instead of downloading",
          "[update][update-fsm]") {
    const std::vector<UpdateEffect> expected{
        ufx::PersistLastCheck{}, ufx::Notify{UpdateNotice::Available, QStringLiteral("0.2.0")},
        ufx::ScheduleNextCheck{kPeriodicIntervalMs}};

    SECTION("auto-download off") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.autoDownload = false;
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK_FALSE(r.next.meteredDeferred);
        CHECK(r.effects == expected);
    }
    SECTION("metered link defers the auto-download") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.metered = true;
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.next.meteredDeferred); // resumes by itself when the link changes
        CHECK(r.effects == expected);
    }
    SECTION("a portable copy never applies anything") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.portable = true;
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK_FALSE(r.next.meteredDeferred);
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: a manifest at or below the running version is UpToDate",
          "[update][update-fsm]") {
    for (const char* version : {"0.1.0", "0.0.9"}) {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.availableVersion = QStringLiteral("0.2.0"); // a stale announcement
        s.totalBytes = 1;
        const auto r =
            reduceUpdate(s, uev::ManifestArrived{manifest(QString::fromLatin1(version))});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        CHECK(r.next.availableVersion.isEmpty());
        CHECK(r.next.totalBytes == 0);
        const std::vector<UpdateEffect> expected{ufx::PersistLastCheck{},
                                                 ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: the yank rule discards a stage the newest manifest disowns",
          "[update][update-fsm]") {
    SECTION("the release was pulled: the manifest is older than the stage") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.stagedVersion = QStringLiteral("0.3.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.stagedVersion.isEmpty());
        CHECK(r.next.phase == UpdatePhase::Downloading); // 0.2.0 is still newer than 0.1.0
        const std::vector<UpdateEffect> expected{
            ufx::PersistLastCheck{}, ufx::DiscardStaged{QStringLiteral("0.3.0")},
            ufx::StartDownload{assetUrl(QStringLiteral("0.2.0")), kSha, 41943040,
                               QStringLiteral("0.2.0")},
            ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
        CHECK(r.effects == expected);
    }
    SECTION("the stage was superseded") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.stagedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.3.0"))});
        CHECK(r.next.stagedVersion.isEmpty());
        CHECK(r.next.phase == UpdatePhase::Downloading);
        CHECK(r.effects.at(1) == UpdateEffect{ufx::DiscardStaged{QStringLiteral("0.2.0")}});
    }
    SECTION("the machine already caught up: nothing is kept") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.currentVersion = QStringLiteral("0.3.0");
        s.stagedVersion = QStringLiteral("0.3.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.3.0"))});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        CHECK(r.next.stagedVersion.isEmpty());
        const std::vector<UpdateEffect> expected{ufx::PersistLastCheck{},
                                                 ufx::DiscardStaged{QStringLiteral("0.3.0")},
                                                 ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
        CHECK(r.effects == expected);
    }
    SECTION("a stage that IS the offered version survives untouched") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.stagedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::Ready);
        CHECK(r.next.stagedVersion == QStringLiteral("0.2.0"));
        CHECK(r.next.receivedBytes == r.next.totalBytes);
        const std::vector<UpdateEffect> expected{
            ufx::PersistLastCheck{}, ufx::Notify{UpdateNotice::Ready, QStringLiteral("0.2.0")},
            ufx::ScheduleNextCheck{kPeriodicIntervalMs}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: a build below the supported minimum is required",
          "[update][update-fsm]") {
    const auto r = reduceUpdate(
        base(UpdatePhase::Checking),
        uev::ManifestArrived{manifest(QStringLiteral("0.3.0"), QStringLiteral("0.2.0"))});
    CHECK(r.next.required);
    CHECK(r.next.minimumSupportedVersion == QStringLiteral("0.2.0"));
    REQUIRE(r.effects.size() >= 2);
    // The unsupported notice comes before the phase's own effects.
    CHECK(r.effects.at(1) ==
          UpdateEffect{ufx::Notify{UpdateNotice::Unsupported, QStringLiteral("0.3.0")}});
}

TEST_CASE("update machine: a skipped version is muted unless it is required",
          "[update][update-fsm]") {
    SECTION("muted") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.skippedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        CHECK(r.next.availableVersion.isEmpty());
    }
    SECTION("a skip is ignored while the running build is unsupported") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.skippedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(
            s, uev::ManifestArrived{manifest(QStringLiteral("0.2.0"), QStringLiteral("0.2.0"))});
        CHECK(r.next.phase == UpdatePhase::Downloading);
        CHECK(r.next.required);
    }
    SECTION("skipping one version does not mute the next") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.skippedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(s, uev::ManifestArrived{manifest(QStringLiteral("0.3.0"))});
        CHECK(r.next.phase == UpdatePhase::Downloading);
    }
}

TEST_CASE("update machine: a manifest that arrives outside a check is ignored",
          "[update][update-fsm]") {
    for (const UpdatePhase phase :
         {UpdatePhase::Idle, UpdatePhase::Ready, UpdatePhase::Downloading, UpdatePhase::Disabled}) {
        const auto r =
            reduceUpdate(base(phase), uev::ManifestArrived{manifest(QStringLiteral("9.9.9"))});
        CHECK(r.next.phase == phase);
        CHECK(r.effects.empty());
    }
}

TEST_CASE("update machine: a failed check backs off without losing a ready stage",
          "[update][update-fsm]") {
    SECTION("nothing staged: the failure is what the user sees") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.consecutiveFailures = 2;
        const auto r = reduceUpdate(s, uev::CheckFailed{UpdateError::Http});
        CHECK(r.next.phase == UpdatePhase::Failed);
        CHECK(r.next.error == UpdateError::Http);
        CHECK(r.next.consecutiveFailures == 3);
        const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{backoffDelayMs(3)}};
        CHECK(r.effects == expected);
    }
    SECTION("a verified stage still applies at the next start") {
        UpdateStatus s = base(UpdatePhase::Checking);
        s.stagedVersion = QStringLiteral("0.2.0");
        s.totalBytes = 41943040;
        const auto r = reduceUpdate(s, uev::CheckFailed{UpdateError::Offline});
        CHECK(r.next.phase == UpdatePhase::Ready);
        // The pill must not carry a stale error token while it says "ready".
        CHECK(r.next.error == UpdateError::None);
        CHECK(r.next.receivedBytes == r.next.totalBytes);
        CHECK(r.next.consecutiveFailures == 1);
        const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{backoffDelayMs(1)}};
        CHECK(r.effects == expected);
    }
    SECTION("a failure outside a check is ignored") {
        const auto r = reduceUpdate(base(UpdatePhase::Idle), uev::CheckFailed{UpdateError::Http});
        CHECK(r.next.phase == UpdatePhase::Idle);
        CHECK(r.effects.empty());
    }
}

// ── Download lifecycle ──────────────────────────────────────────────────────

TEST_CASE("update machine: a manual download is allowed from Available only",
          "[update][update-fsm]") {
    const auto r = reduceUpdate(available(), uev::DownloadRequested{});
    CHECK(r.next.phase == UpdatePhase::Downloading);
    CHECK(r.next.error == UpdateError::None);
    const std::vector<UpdateEffect> expected{ufx::StartDownload{
        assetUrl(QStringLiteral("0.2.0")), kSha, 41943040, QStringLiteral("0.2.0")}};
    CHECK(r.effects == expected);

    SECTION("a metered link does not block an explicit request") {
        UpdateStatus s = available();
        s.metered = true;
        s.meteredDeferred = true;
        const auto metered = reduceUpdate(s, uev::DownloadRequested{});
        CHECK(metered.next.phase == UpdatePhase::Downloading);
        CHECK_FALSE(metered.next.meteredDeferred);
    }
    SECTION("a portable copy has nothing to apply it to") {
        UpdateStatus s = available();
        s.portable = true;
        const auto portable = reduceUpdate(s, uev::DownloadRequested{});
        CHECK(portable.next.phase == UpdatePhase::Available);
        CHECK(portable.effects.empty());
    }
    SECTION("no asset, no download") {
        UpdateStatus s = available();
        s.availableAsset = UpdateAsset{};
        const auto empty = reduceUpdate(s, uev::DownloadRequested{});
        CHECK(empty.next.phase == UpdatePhase::Available);
        CHECK(empty.effects.empty());
    }
    SECTION("every other phase ignores it") {
        for (const UpdatePhase phase :
             {UpdatePhase::Idle, UpdatePhase::Checking, UpdatePhase::Ready, UpdatePhase::Failed}) {
            const auto ignored = reduceUpdate(base(phase), uev::DownloadRequested{});
            CHECK(ignored.next.phase == phase);
            CHECK(ignored.effects.empty());
        }
    }
}

TEST_CASE("update machine: transfer progress only moves while downloading",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Downloading;

    const auto started = reduceUpdate(s, uev::DownloadStarted{1234});
    CHECK(started.next.totalBytes == 1234u);
    CHECK(started.effects.empty());

    const auto progress = reduceUpdate(s, uev::DownloadProgress{4096});
    CHECK(progress.next.receivedBytes == 4096u);
    CHECK(progress.effects.empty());

    // Late callbacks after the phase moved on change nothing.
    CHECK(reduceUpdate(base(UpdatePhase::Ready), uev::DownloadProgress{99}).next.receivedBytes ==
          0u);
    CHECK(reduceUpdate(base(UpdatePhase::Idle), uev::DownloadStarted{99}).next.totalBytes == 0u);
}

TEST_CASE("update machine: a finished download is verified from disk, never from the stream",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Downloading;
    const auto r = reduceUpdate(s, uev::DownloadFinished{QStringLiteral("C:/part")});
    CHECK(r.next.phase == UpdatePhase::Verifying);
    const std::vector<UpdateEffect> expected{ufx::VerifyAndPromote{QStringLiteral("0.2.0"), kSha}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: a failed download backs off like a failed check",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Downloading;
    s.receivedBytes = 999;
    const auto r = reduceUpdate(s, uev::DownloadFailed{UpdateError::DiskFull});
    CHECK(r.next.phase == UpdatePhase::Failed);
    CHECK(r.next.error == UpdateError::DiskFull);
    CHECK(r.next.receivedBytes == 0);
    CHECK(r.next.consecutiveFailures == 1);
    const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{backoffDelayMs(1)}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: a verified promote lands Ready and sweeps", "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Verifying;
    s.consecutiveFailures = 4;
    const auto r = reduceUpdate(s, uev::VerifyOk{QStringLiteral("C:/ready/0.2.0")});
    CHECK(r.next.phase == UpdatePhase::Ready);
    CHECK(r.next.stagedVersion == QStringLiteral("0.2.0"));
    CHECK(r.next.receivedBytes == r.next.totalBytes);
    CHECK(r.next.consecutiveFailures == 0);
    CHECK(r.next.error == UpdateError::None);
    const std::vector<UpdateEffect> expected{
        ufx::Notify{UpdateNotice::Ready, QStringLiteral("0.2.0")}, ufx::SweepStaging{}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: bytes that fail the on-disk re-hash are discarded entirely",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Verifying;
    const auto r = reduceUpdate(s, uev::VerifyFailed{});
    CHECK(r.next.phase == UpdatePhase::Failed);
    CHECK(r.next.error == UpdateError::Corrupt);
    CHECK(r.next.stagedVersion.isEmpty());
    // No resume, nothing partial kept: the next cycle starts from zero.
    const std::vector<UpdateEffect> expected{ufx::DiscardStaged{QStringLiteral("0.2.0")},
                                             ufx::ScheduleNextCheck{backoffDelayMs(1)}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: verify results outside Verifying are ignored", "[update][update-fsm]") {
    CHECK(reduceUpdate(base(UpdatePhase::Ready), uev::VerifyFailed{}).effects.empty());
    CHECK(reduceUpdate(base(UpdatePhase::Idle), uev::VerifyOk{QStringLiteral("C:/x")})
              .effects.empty());
}

// ── Staging discovered out of band ──────────────────────────────────────────

TEST_CASE("update machine: a staged build found at startup goes straight to Ready",
          "[update][update-fsm]") {
    for (const UpdatePhase phase :
         {UpdatePhase::Idle, UpdatePhase::UpToDate, UpdatePhase::Failed}) {
        const auto r = reduceUpdate(
            base(phase), uev::StagedFound{QStringLiteral("0.2.0"), QStringLiteral("C:/ready")});
        CHECK(r.next.phase == UpdatePhase::Ready);
        CHECK(r.next.stagedVersion == QStringLiteral("0.2.0"));
        CHECK(r.next.availableVersion == QStringLiteral("0.2.0"));
        CHECK(r.next.error == UpdateError::None);
        const std::vector<UpdateEffect> expected{
            ufx::Notify{UpdateNotice::Ready, QStringLiteral("0.2.0")}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: a stage that is not usable is discarded on sight",
          "[update][update-fsm]") {
    SECTION("at or below the running version") {
        for (const char* version : {"0.1.0", "0.0.9"}) {
            const auto r = reduceUpdate(
                base(), uev::StagedFound{QString::fromLatin1(version), QStringLiteral("C:/ready")});
            CHECK(r.next.stagedVersion.isEmpty());
            const std::vector<UpdateEffect> expected{
                ufx::DiscardStaged{QString::fromLatin1(version)}};
            CHECK(r.effects == expected);
        }
    }
    SECTION("explicitly skipped") {
        UpdateStatus s = base();
        s.skippedVersion = QStringLiteral("0.2.0");
        const auto r =
            reduceUpdate(s, uev::StagedFound{QStringLiteral("0.2.0"), QStringLiteral("C:/ready")});
        CHECK(r.next.stagedVersion.isEmpty());
        const std::vector<UpdateEffect> expected{ufx::DiscardStaged{QStringLiteral("0.2.0")}};
        CHECK(r.effects == expected);
    }
    SECTION("an unparsable version") {
        const auto r = reduceUpdate(
            base(), uev::StagedFound{QStringLiteral("banana"), QStringLiteral("C:/ready")});
        CHECK(r.next.stagedVersion.isEmpty());
        const std::vector<UpdateEffect> expected{ufx::DiscardStaged{QStringLiteral("banana")}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("update machine: a stage found mid-cycle is recorded without a phase change",
          "[update][update-fsm]") {
    for (const UpdatePhase phase :
         {UpdatePhase::Checking, UpdatePhase::Downloading, UpdatePhase::Disabled}) {
        const auto r = reduceUpdate(
            base(phase), uev::StagedFound{QStringLiteral("0.2.0"), QStringLiteral("C:/ready")});
        CHECK(r.next.phase == phase);
        CHECK(r.next.stagedVersion == QStringLiteral("0.2.0"));
        CHECK(r.effects.empty());
    }
}

TEST_CASE("update machine: an invalidated stage clears Ready back to Idle",
          "[update][update-fsm]") {
    UpdateStatus s = base(UpdatePhase::Ready);
    s.stagedVersion = QStringLiteral("0.2.0");
    s.receivedBytes = 41943040;
    const auto r = reduceUpdate(s, uev::StagedInvalidated{});
    CHECK(r.next.phase == UpdatePhase::Idle);
    CHECK(r.next.stagedVersion.isEmpty());
    CHECK(r.next.receivedBytes == 0);
    CHECK(r.effects.empty());

    // Nothing staged: nothing to invalidate.
    const auto none = reduceUpdate(base(UpdatePhase::UpToDate), uev::StagedInvalidated{});
    CHECK(none.next.phase == UpdatePhase::UpToDate);
}

// ── User decisions ──────────────────────────────────────────────────────────

TEST_CASE("update machine: skipping mutes, un-stages and stops the transfer",
          "[update][update-fsm]") {
    SECTION("from Available") {
        const auto r = reduceUpdate(available(), uev::SkipRequested{QStringLiteral("0.2.0")});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        CHECK(r.next.skippedVersion == QStringLiteral("0.2.0"));
        CHECK(r.next.availableVersion.isEmpty());
        CHECK(r.next.totalBytes == 0);
        CHECK(r.effects.empty());
    }
    SECTION("mid-download the transfer is aborted") {
        UpdateStatus s = available();
        s.phase = UpdatePhase::Downloading;
        const auto r = reduceUpdate(s, uev::SkipRequested{QStringLiteral("0.2.0")});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        const std::vector<UpdateEffect> expected{ufx::AbortDownload{}};
        CHECK(r.effects == expected);
    }
    SECTION("an already staged build is discarded, not left to apply at boot") {
        UpdateStatus s = available();
        s.phase = UpdatePhase::Ready;
        s.stagedVersion = QStringLiteral("0.2.0");
        const auto r = reduceUpdate(s, uev::SkipRequested{QStringLiteral("0.2.0")});
        CHECK(r.next.phase == UpdatePhase::UpToDate);
        CHECK(r.next.stagedVersion.isEmpty());
        const std::vector<UpdateEffect> expected{ufx::DiscardStaged{QStringLiteral("0.2.0")}};
        CHECK(r.effects == expected);
    }
    SECTION("a required update cannot be skipped") {
        UpdateStatus s = available();
        s.required = true;
        const auto r = reduceUpdate(s, uev::SkipRequested{QStringLiteral("0.2.0")});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.next.skippedVersion.isEmpty());
        CHECK(r.effects.empty());
    }
    SECTION("an empty version is not a skip") {
        const auto r = reduceUpdate(available(), uev::SkipRequested{QString()});
        CHECK(r.next.skippedVersion.isEmpty());
        CHECK(r.effects.empty());
    }
    SECTION("skipping a version that is not the announced one only records the mute") {
        const auto r = reduceUpdate(available(), uev::SkipRequested{QStringLiteral("0.9.0")});
        CHECK(r.next.phase == UpdatePhase::Available);
        CHECK(r.next.skippedVersion == QStringLiteral("0.9.0"));
        CHECK(r.next.availableVersion == QStringLiteral("0.2.0"));
        CHECK(r.effects.empty());
    }
}

// ── Connectivity ────────────────────────────────────────────────────────────

TEST_CASE("update machine: leaving a metered link resumes a deferred download",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.metered = true;
    s.meteredDeferred = true;
    const auto r = reduceUpdate(s, uev::MeteredChanged{false});
    CHECK(r.next.phase == UpdatePhase::Downloading);
    CHECK_FALSE(r.next.metered);
    CHECK_FALSE(r.next.meteredDeferred);
    const std::vector<UpdateEffect> expected{ufx::StartDownload{
        assetUrl(QStringLiteral("0.2.0")), kSha, 41943040, QStringLiteral("0.2.0")}};
    CHECK(r.effects == expected);
}

TEST_CASE("update machine: a link that turns metered mid-download is left alone",
          "[update][update-fsm]") {
    UpdateStatus s = available();
    s.phase = UpdatePhase::Downloading;
    s.receivedBytes = 1024 * 1024;
    const auto r = reduceUpdate(s, uev::MeteredChanged{true});
    CHECK(r.next.phase == UpdatePhase::Downloading);
    CHECK(r.next.metered);
    CHECK(r.next.receivedBytes == 1024u * 1024u); // bytes already paid for
    CHECK(r.effects.empty());
}

TEST_CASE("update machine: an unmetered link with nothing deferred just records it",
          "[update][update-fsm]") {
    const auto r = reduceUpdate(available(), uev::MeteredChanged{false});
    CHECK(r.next.phase == UpdatePhase::Available);
    CHECK(r.effects.empty());

    UpdateStatus portable = available();
    portable.portable = true;
    portable.metered = true;
    portable.meteredDeferred = true;
    const auto never = reduceUpdate(portable, uev::MeteredChanged{false});
    CHECK(never.next.phase == UpdatePhase::Available);
    CHECK(never.effects.empty());
}

TEST_CASE("update machine: coming back online re-arms the check that the gate refused",
          "[update][update-fsm]") {
    UpdateStatus s = base(UpdatePhase::Failed);
    s.error = UpdateError::Offline;
    s.online = false;
    const auto r = reduceUpdate(s, uev::ReachabilityChanged{true});
    CHECK(r.next.online);
    const std::vector<UpdateEffect> expected{ufx::ScheduleNextCheck{kReconnectCheckDelayMs}};
    CHECK(r.effects == expected);

    SECTION("a failure that was not the offline gate does not get the fast path") {
        UpdateStatus http = base(UpdatePhase::Failed);
        http.error = UpdateError::Http;
        http.online = false;
        const auto ignored = reduceUpdate(http, uev::ReachabilityChanged{true});
        CHECK(ignored.effects.empty());
    }
    SECTION("going offline is recorded without any effect") {
        const auto offline = reduceUpdate(base(), uev::ReachabilityChanged{false});
        CHECK_FALSE(offline.next.online);
        CHECK(offline.effects.empty());
    }
    SECTION("a disabled client re-arms nothing") {
        UpdateStatus disabled = base(UpdatePhase::Failed);
        disabled.error = UpdateError::Offline;
        disabled.checksEnabled = false;
        const auto r2 = reduceUpdate(disabled, uev::ReachabilityChanged{true});
        CHECK(r2.effects.empty());
    }
}

// ── Totality ────────────────────────────────────────────────────────────────

TEST_CASE("update machine: every phase-event pair is total", "[update][update-fsm]") {
    const std::vector<UpdatePhase> phases{
        UpdatePhase::Disabled,  UpdatePhase::Idle,      UpdatePhase::Checking,
        UpdatePhase::UpToDate,  UpdatePhase::Available, UpdatePhase::Downloading,
        UpdatePhase::Verifying, UpdatePhase::Ready,     UpdatePhase::Failed};
    const std::vector<dish::reducer::UpdateEvent> events{
        uev::PrefsChanged{true, true, QString()},
        uev::PrefsChanged{false, false, QStringLiteral("0.2.0")},
        uev::CheckRequested{UpdateTrigger::Periodic},
        uev::ManifestArrived{manifest(QStringLiteral("0.2.0"))},
        uev::CheckFailed{UpdateError::Http},
        uev::DownloadRequested{},
        uev::DownloadStarted{10},
        uev::DownloadProgress{5},
        uev::DownloadFinished{QStringLiteral("C:/part")},
        uev::DownloadFailed{UpdateError::Io},
        uev::VerifyOk{QStringLiteral("C:/ready")},
        uev::VerifyFailed{},
        uev::StagedFound{QStringLiteral("0.2.0"), QStringLiteral("C:/ready")},
        uev::StagedInvalidated{},
        uev::SkipRequested{QStringLiteral("0.2.0")},
        uev::MeteredChanged{true},
        uev::ReachabilityChanged{false},
    };

    for (const UpdatePhase phase : phases) {
        for (const auto& event : events) {
            const auto r = reduceUpdate(available(), event);
            Q_UNUSED(r);
            const auto fromPhase = reduceUpdate(base(phase), event);
            // Total: a status always comes back, and the phase is one of the
            // nine.
            CHECK(static_cast<int>(fromPhase.next.phase) >=
                  static_cast<int>(UpdatePhase::Disabled));
            CHECK(static_cast<int>(fromPhase.next.phase) <= static_cast<int>(UpdatePhase::Failed));
        }
    }
}
