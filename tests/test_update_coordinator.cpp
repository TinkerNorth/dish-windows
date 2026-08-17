// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The coordinator over fake ports: the startup schedule and its 1 h gap, the
// jittered backoff, the manual rate limit, the disk preflight, and a whole
// check -> download -> verify -> ready cycle with no sockets and no staging
// directory. The test constructor runs every port inline on the calling thread,
// so a full cycle is assertable without spinning an event loop.
//
// Two pieces of process state are borrowed and put back by the fixture: the
// QSettings location (redirected to an INI under a temp directory, because the
// coordinator's bookkeeping keys live in the same hive as the user's real
// preferences) and, when the build tree has none, an `unins000.exe` next to
// the test binary — the portable/managed probe is a filesystem check for an
// Inno Setup uninstaller sibling, and a portable copy is notify-only by
// design.
//
// The cases that drive a check deliberately do NOT call start(): start() also
// asks QNetworkInformation for reachability, and a developer machine behind a
// captive portal would otherwise change the outcome.

#include "update/UpdateCoordinator.h"

#include "core/reducer/UpdateMachine.h"
#include "core/update/UpdateManifest.h"
#include "source/store/UpdatePreferenceStore.h"
#include "update/UpdatePorts.h"

#include "StateSourceProbe.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

#include <memory>
#include <optional>
#include <utility>

// Pulls windows.h, so it stays last: no Win32 macro may reach the Qt headers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using dish::reducer::backoffDelayMs;
using dish::reducer::kBackoffJitter;
using dish::reducer::kManualMinGapMs;
using dish::reducer::kMinCheckGapMs;
using dish::reducer::kPeriodicIntervalMs;
using dish::reducer::kStartupDelayMs;
using dish::reducer::UpdateError;
using dish::reducer::UpdateNotice;
using dish::reducer::UpdatePhase;
using dish::reducer::UpdateStatus;
using dish::source::kKeyUpdatesLastCheckUtcMs;
using dish::source::kKeyUpdatesLastRunVersion;
using dish::source::UpdatePreferences;
using dish::source::UpdatePreferenceStore;
using dish::test::StateSourceProbe;
using dish::update::DownloadGateway;
using dish::update::DownloadOutcome;
using dish::update::DownloadRequest;
using dish::update::ManifestFetchResult;
using dish::update::ManifestGateway;
using dish::update::StagedUpdate;
using dish::update::StagingStore;
using dish::update::UpdateCoordinator;
using dish::update::UpdateCoordinatorPorts;
using dish::update::UpdateManifest;

namespace {

const QString kSha = QString(64, QLatin1Char('a'));
const QString kCurrent = QStringLiteral("0.1.0");

// Catch2WithMain creates no QCoreApplication; the coordinator's timer and its
// aboutToQuit hook both want one.
void ensureApp() {
    if (QCoreApplication::instance() != nullptr) { return; }
    static int argc = 1;
    static char arg0[] = "DishTests";
    static char* argv[] = {arg0, nullptr};
    static QCoreApplication app(argc, argv);
}

UpdateManifest manifest(const QString& version, const QString& minimum = QStringLiteral("0.1.0")) {
    UpdateManifest m;
    m.schema = 1;
    m.product = QStringLiteral("dish-windows");
    m.version = version;
    m.channel = QStringLiteral("stable");
    m.publishedAt = QStringLiteral("2026-08-03T14:21:07Z");
    m.minimumSupportedVersion = minimum;
    m.releaseNotesUrl =
        QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/tag/v") + version;
    m.setupAsset.url =
        QStringLiteral("https://github.com/TinkerNorth/dish-windows/releases/download/v") +
        version + QStringLiteral("/dish-setup.exe");
    m.setupAsset.sha256 = kSha;
    m.setupAsset.size = 41943040;
    return m;
}

class FakeManifestGateway final : public ManifestGateway {
  public:
    void fetch(Callback done) override {
        ++fetches_;
        if (deliverInline_) {
            done(result_);
        } else {
            pending_ = std::move(done);
        }
    }
    void cancel() override { ++cancels_; }

    void setResult(ManifestFetchResult result) { result_ = std::move(result); }
    void setDeliverInline(bool inlineDelivery) { deliverInline_ = inlineDelivery; }
    void deliverPending() {
        if (!pending_) { return; }
        Callback done;
        done.swap(pending_);
        done(result_);
    }
    int fetches() const { return fetches_; }
    int cancels() const { return cancels_; }

  private:
    ManifestFetchResult result_ = ManifestFetchResult::failed(UpdateError::Http);
    Callback pending_;
    int fetches_ = 0;
    int cancels_ = 0;
    bool deliverInline_ = true;
};

class FakeDownloadGateway final : public DownloadGateway {
  public:
    enum class Mode { Succeed, Fail, Hold };

    void start(const DownloadRequest& request, StartedCallback started, ProgressCallback progress,
               FinishedCallback finished) override {
        ++starts_;
        lastRequest_ = request;
        if (mode_ == Mode::Hold) { return; }
        if (started) { started(request.size); }
        if (mode_ == Mode::Succeed) {
            if (progress) { progress(request.size); }
            if (finished) { finished(DownloadOutcome{true, request.partPath, UpdateError::None}); }
            return;
        }
        if (finished) { finished(DownloadOutcome{false, QString(), error_}); }
    }
    void abort() override { ++aborts_; }

    void setMode(Mode mode) { mode_ = mode; }
    void setError(UpdateError error) { error_ = error; }
    int starts() const { return starts_; }
    int aborts() const { return aborts_; }
    DownloadRequest lastRequest() const { return lastRequest_; }

  private:
    DownloadRequest lastRequest_;
    Mode mode_ = Mode::Succeed;
    UpdateError error_ = UpdateError::Http;
    int starts_ = 0;
    int aborts_ = 0;
};

class FakeStagingStore final : public StagingStore {
  public:
    QString root() const override { return QStringLiteral("C:/fake/updates"); }
    QString partPathFor(const QString& version) const override {
        return root() + QStringLiteral("/staging/dish-setup-") + version +
               QStringLiteral(".exe.part");
    }
    std::optional<StagedUpdate> findStaged() override { return staged_; }

    std::optional<QString> promote(const QString& version, const QString& sha256, qint64 size,
                                   const QByteArray& manifestBytes) override {
        ++promotes_;
        lastManifestBytes_ = manifestBytes;
        if (!promoteOk_) { return std::nullopt; }
        StagedUpdate staged;
        staged.version = version;
        staged.dir = root() + QStringLiteral("/ready/") + version;
        staged.exePath = staged.dir + QStringLiteral("/dish-setup.exe");
        staged.sha256 = sha256;
        staged.size = size;
        staged_ = staged;
        return staged.dir;
    }

    void discard(const QString& version) override {
        discarded_.append(version);
        if (staged_.has_value() && staged_->version == version) { staged_.reset(); }
    }

    void sweep(const QString& currentVersion) override {
        ++sweeps_;
        lastSweepVersion_ = currentVersion;
    }

    bool hasRoomFor(qint64 assetSize) const override {
        lastRoomQuery_ = assetSize;
        return hasRoom_;
    }

    void setStaged(const StagedUpdate& staged) { staged_ = staged; }
    void setPromoteOk(bool ok) { promoteOk_ = ok; }
    void setHasRoom(bool room) { hasRoom_ = room; }
    int promotes() const { return promotes_; }
    int sweeps() const { return sweeps_; }
    QString lastSweepVersion() const { return lastSweepVersion_; }
    QStringList discarded() const { return discarded_; }
    QByteArray lastManifestBytes() const { return lastManifestBytes_; }
    qint64 lastRoomQuery() const { return lastRoomQuery_; }

  private:
    std::optional<StagedUpdate> staged_;
    QStringList discarded_;
    QByteArray lastManifestBytes_;
    QString lastSweepVersion_;
    mutable qint64 lastRoomQuery_ = 0;
    int promotes_ = 0;
    int sweeps_ = 0;
    bool promoteOk_ = true;
    bool hasRoom_ = true;
};

// Serializes the fixture across the separate processes ctest launches per case
// (CI runs `ctest --parallel`): the managed-install marker below is one file in
// one directory, so two cases must not create and delete it at the same time.
// WAIT_ABANDONED still grants ownership, so a crashed case cannot wedge the run.
class MachineStateLock {
  public:
    MachineStateLock() {
        handle_ = ::CreateMutexW(nullptr, FALSE, L"Local\\DishTests.UpdaterMachineState");
        if (handle_ != nullptr) { ::WaitForSingleObject(handle_, 120000); }
    }
    ~MachineStateLock() {
        if (handle_ == nullptr) { return; }
        ::ReleaseMutex(handle_);
        ::CloseHandle(handle_);
    }
    MachineStateLock(const MachineStateLock&) = delete;
    MachineStateLock& operator=(const MachineStateLock&) = delete;

  private:
    HANDLE handle_ = nullptr;
};

// Redirects QSettings at a temp INI (the coordinator's own settings_ member is
// a default-constructed QSettings, which honours the default format) and makes
// the build tree look like a managed install.
class Fixture {
  public:
    Fixture() {
        ensureApp();
        savedFormat_ = QSettings::defaultFormat();
        savedOrganization_ = QCoreApplication::organizationName();
        savedApplication_ = QCoreApplication::applicationName();
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, dir_.path());
        QCoreApplication::setOrganizationName(QStringLiteral("DishTests"));
        QCoreApplication::setApplicationName(QStringLiteral("UpdateCoordinator"));

        markerPath_ = QCoreApplication::applicationDirPath() + QStringLiteral("/unins000.exe");
        if (!QFileInfo::exists(markerPath_)) {
            QFile marker(markerPath_);
            if (marker.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                marker.write("managed-install probe");
                marker.close();
                createdMarker_ = true;
            }
        }
    }

    ~Fixture() {
        if (createdMarker_) { QFile::remove(markerPath_); }
        QSettings::setDefaultFormat(savedFormat_);
        QCoreApplication::setOrganizationName(savedOrganization_);
        QCoreApplication::setApplicationName(savedApplication_);
    }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    bool valid() const { return dir_.isValid(); }
    bool managed() const { return QFileInfo::exists(markerPath_); }

    void seedSetting(const char* key, const QVariant& value) {
        QSettings settings;
        settings.setValue(QLatin1String(key), value);
        settings.sync();
    }

    QVariant readSetting(const char* key) {
        QSettings settings;
        return settings.value(QLatin1String(key));
    }

    std::unique_ptr<UpdatePreferenceStore> makePrefs(bool checksEnabled = true,
                                                     bool autoDownload = true) {
        auto settings = std::make_unique<QSettings>(dir_.filePath(QStringLiteral("prefs.ini")),
                                                    QSettings::IniFormat);
        settings->setValue(QLatin1String(UpdatePreferenceStore::kKeyChecksEnabled), checksEnabled);
        settings->setValue(QLatin1String(UpdatePreferenceStore::kKeyAutoDownload), autoDownload);
        settings->sync();
        return std::make_unique<UpdatePreferenceStore>(std::move(settings));
    }

  private:
    // First member: acquired before anything is borrowed, released last.
    MachineStateLock lock_;
    QTemporaryDir dir_;
    QString markerPath_;
    QSettings::Format savedFormat_ = QSettings::NativeFormat;
    QString savedOrganization_;
    QString savedApplication_;
    bool createdMarker_ = false;
};

// Everything a case needs, wired the way AppModel wires it.
struct Harness {
    FakeManifestGateway* manifest = nullptr;
    FakeDownloadGateway* download = nullptr;
    FakeStagingStore* staging = nullptr;
    std::unique_ptr<UpdateCoordinator> coordinator;
    int noticeCount = 0;
    QStringList notices;

    void build(UpdatePreferenceStore* prefs, qint64 nowMs) {
        auto manifestGateway = std::make_unique<FakeManifestGateway>();
        auto downloadGateway = std::make_unique<FakeDownloadGateway>();
        auto stagingStore = std::make_unique<FakeStagingStore>();
        manifest = manifestGateway.get();
        download = downloadGateway.get();
        staging = stagingStore.get();

        UpdateCoordinatorPorts ports;
        ports.manifest = std::move(manifestGateway);
        ports.download = std::move(downloadGateway);
        ports.staging = std::move(stagingStore);

        coordinator = std::make_unique<UpdateCoordinator>(prefs, std::move(ports));
        coordinator->setClock([nowMs] { return nowMs; });
        coordinator->setCurrentVersion(kCurrent);
        QObject::connect(coordinator.get(), &UpdateCoordinator::notice,
                         [this](UpdateNotice notice, const QString& version) {
                             ++noticeCount;
                             notices.append(QString::number(static_cast<int>(notice)) +
                                            QLatin1Char('/') + version);
                         });
    }
};

const qint64 kNow =
    QDateTime::fromString(QStringLiteral("2026-08-04T12:00:00Z"), Qt::ISODate).toMSecsSinceEpoch();

} // namespace

TEST_CASE("update coordinator: the startup check is armed 15 s out", "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    CHECK(harness.coordinator->pendingCheckDelayMs() == -1);
    harness.coordinator->start();
    CHECK(harness.coordinator->pendingCheckDelayMs() == kStartupDelayMs);
    // Nothing has gone near the network yet: the delay is the whole point.
    CHECK(harness.manifest->fetches() == 0);
    // The janitor runs before the staged scan, so a condemned stage is never
    // reported as found.
    CHECK(harness.staging->sweeps() >= 1);
    CHECK(harness.staging->lastSweepVersion() == kCurrent);
}

TEST_CASE("update coordinator: a check inside the 1 h gap waits for the periodic tick",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    fixture.seedSetting(kKeyUpdatesLastCheckUtcMs, kNow - (10 * 60 * 1000));
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    harness.coordinator->start();
    CHECK(harness.coordinator->pendingCheckDelayMs() == kPeriodicIntervalMs);
}

TEST_CASE("update coordinator: a check older than the gap starts on the 15 s delay",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    fixture.seedSetting(kKeyUpdatesLastCheckUtcMs, kNow - kMinCheckGapMs - 1);
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    harness.coordinator->start();
    CHECK(harness.coordinator->pendingCheckDelayMs() == kStartupDelayMs);
}

TEST_CASE("update coordinator: a clock that reads far in the future checks anyway",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    // A recorded time 25 h ahead means the clock moved, not that a check just
    // happened; going quiet for a day would be the wrong answer.
    fixture.seedSetting(kKeyUpdatesLastCheckUtcMs, kNow + (25LL * 60 * 60 * 1000));
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    harness.coordinator->start();
    CHECK(harness.coordinator->pendingCheckDelayMs() == kStartupDelayMs);
}

TEST_CASE("update coordinator: checks disabled arms nothing and touches no gateway",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs(/*checksEnabled=*/false);
    Harness harness;
    harness.build(prefs.get(), kNow);

    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Disabled);
    harness.coordinator->start();
    CHECK(harness.coordinator->pendingCheckDelayMs() == -1);

    // Even an explicit request stays inert while checks are off.
    harness.coordinator->checkNow();
    CHECK(harness.manifest->fetches() == 0);
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Disabled);
}

TEST_CASE("update coordinator: a whole cycle from check to ready", "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    if (!fixture.managed()) {
        SUCCEED("skipped: the build tree could not be made to look like a managed install");
        return;
    }
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray("{\"schema\":1}")));

    StateSourceProbe<UpdateStatus> probe(harness.coordinator->status());
    const std::size_t before = probe.count();

    harness.coordinator->checkNow();

    const UpdateStatus status = harness.coordinator->snapshot();
    CHECK(status.phase == UpdatePhase::Ready);
    CHECK(status.availableVersion == QStringLiteral("0.2.0"));
    CHECK(status.stagedVersion == QStringLiteral("0.2.0"));
    CHECK(status.receivedBytes == status.totalBytes);
    CHECK(status.error == UpdateError::None);
    CHECK_FALSE(status.portable);

    CHECK(harness.manifest->fetches() == 1);
    CHECK(harness.download->starts() == 1);
    CHECK(harness.download->lastRequest().version == QStringLiteral("0.2.0"));
    CHECK(harness.download->lastRequest().sha256 == kSha);
    CHECK(harness.download->lastRequest().partPath ==
          harness.staging->partPathFor(QStringLiteral("0.2.0")));
    CHECK(harness.staging->promotes() == 1);
    // The manifest that described the bytes is snapshotted beside them.
    CHECK(harness.staging->lastManifestBytes() == QByteArray("{\"schema\":1}"));
    // The ready notice fires exactly once.
    CHECK(harness.noticeCount == 1);
    CHECK(harness.notices.first().endsWith(QStringLiteral("/0.2.0")));
    // The successful check is persisted for the next start's 1 h gap.
    CHECK(fixture.readSetting(kKeyUpdatesLastCheckUtcMs).toLongLong() == kNow);
    CHECK(harness.coordinator->lastCheck().toMSecsSinceEpoch() == kNow);

    // One transition per REAL change: checking, downloading, verifying, ready.
    // (Byte progress on a 40 MB asset arrives as one callback from the fake.)
    const std::size_t transitions = probe.count() - before;
    CHECK(transitions >= 4);
    CHECK(transitions <= 6);
}

TEST_CASE("update coordinator: the disk preflight refuses before any transfer",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    if (!fixture.managed()) {
        SUCCEED("skipped: portable copies never download");
        return;
    }
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));
    harness.staging->setHasRoom(false);

    harness.coordinator->checkNow();

    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Failed);
    CHECK(harness.coordinator->snapshot().error == UpdateError::DiskFull);
    CHECK(harness.download->starts() == 0);
    CHECK(harness.staging->lastRoomQuery() == 41943040);
}

TEST_CASE("update coordinator: a promote that fails discards and reports Corrupt",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    if (!fixture.managed()) {
        SUCCEED("skipped: portable copies never download");
        return;
    }
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));
    harness.staging->setPromoteOk(false);

    harness.coordinator->checkNow();

    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Failed);
    CHECK(harness.coordinator->snapshot().error == UpdateError::Corrupt);
    CHECK(harness.staging->discarded().contains(QStringLiteral("0.2.0")));
}

TEST_CASE("update coordinator: a failed check backs off with jitter", "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(ManifestFetchResult::failed(UpdateError::Http));

    harness.coordinator->checkNow();

    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Failed);
    CHECK(harness.coordinator->snapshot().error == UpdateError::Http);
    CHECK(harness.coordinator->snapshot().consecutiveFailures == 1);

    const int base = backoffDelayMs(1);
    const int armed = harness.coordinator->pendingCheckDelayMs();
    CHECK(armed >= static_cast<int>(base * (1.0 - kBackoffJitter)));
    CHECK(armed <= static_cast<int>(base * (1.0 + kBackoffJitter)));
    // A failed check writes no last-check time: the gap must not swallow the
    // retry.
    CHECK(fixture.readSetting(kKeyUpdatesLastCheckUtcMs).toLongLong() == 0);
}

TEST_CASE("update coordinator: a manual check is rate-limited to one per 10 s",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    qint64 now = kNow;
    Harness harness;
    auto manifestGateway = std::make_unique<FakeManifestGateway>();
    auto downloadGateway = std::make_unique<FakeDownloadGateway>();
    auto stagingStore = std::make_unique<FakeStagingStore>();
    harness.manifest = manifestGateway.get();
    harness.download = downloadGateway.get();
    harness.staging = stagingStore.get();
    UpdateCoordinatorPorts ports;
    ports.manifest = std::move(manifestGateway);
    ports.download = std::move(downloadGateway);
    ports.staging = std::move(stagingStore);
    harness.coordinator = std::make_unique<UpdateCoordinator>(prefs.get(), std::move(ports));
    harness.coordinator->setClock([&now] { return now; });
    harness.coordinator->setCurrentVersion(kCurrent);
    harness.manifest->setResult(ManifestFetchResult::failed(UpdateError::Http));

    harness.coordinator->checkNow();
    CHECK(harness.manifest->fetches() == 1);

    // A held-down button cannot hammer the permalink.
    harness.coordinator->checkNow();
    CHECK(harness.manifest->fetches() == 1);

    now += kManualMinGapMs;
    harness.coordinator->checkNow();
    CHECK(harness.manifest->fetches() == 2);
}

TEST_CASE("update coordinator: a check already in flight is not restarted",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setDeliverInline(false);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));

    harness.coordinator->checkNow();
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Checking);
    CHECK(harness.manifest->fetches() == 1);

    harness.coordinator->firePendingCheck(); // a periodic tick lands mid-check
    CHECK(harness.manifest->fetches() == 1);

    harness.manifest->deliverPending();
    CHECK(harness.coordinator->snapshot().phase != UpdatePhase::Checking);
}

TEST_CASE("update coordinator: a staged build found at startup is announced",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    StagedUpdate staged;
    staged.version = QStringLiteral("0.2.0");
    staged.dir = QStringLiteral("C:/fake/updates/ready/0.2.0");
    staged.exePath = staged.dir + QStringLiteral("/dish-setup.exe");
    staged.sha256 = kSha;
    staged.size = 41943040;
    harness.staging->setStaged(staged);

    harness.coordinator->start();

    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Ready);
    CHECK(harness.coordinator->snapshot().stagedVersion == QStringLiteral("0.2.0"));
    CHECK(harness.noticeCount == 1);
}

TEST_CASE("update coordinator: skipping mutes the version and discards its stage",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    if (!fixture.managed()) {
        SUCCEED("skipped: portable copies never stage");
        return;
    }
    const auto prefs = fixture.makePrefs(/*checksEnabled=*/true, /*autoDownload=*/false);
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));

    harness.coordinator->checkNow();
    REQUIRE(harness.coordinator->snapshot().phase == UpdatePhase::Available);

    harness.coordinator->skipAvailableVersion();
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::UpToDate);
    CHECK(prefs->skippedVersion() == QStringLiteral("0.2.0"));
    CHECK(harness.coordinator->snapshot().availableVersion.isEmpty());

    // And a second call with nothing announced is a no-op.
    harness.coordinator->skipAvailableVersion();
    CHECK(prefs->skippedVersion() == QStringLiteral("0.2.0"));
}

TEST_CASE("update coordinator: a manual download starts the transfer that auto-download skipped",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    if (!fixture.managed()) {
        SUCCEED("skipped: portable copies never download");
        return;
    }
    const auto prefs = fixture.makePrefs(/*checksEnabled=*/true, /*autoDownload=*/false);
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));

    harness.coordinator->checkNow();
    REQUIRE(harness.coordinator->snapshot().phase == UpdatePhase::Available);
    CHECK(harness.download->starts() == 0);

    harness.coordinator->downloadNow();
    CHECK(harness.download->starts() == 1);
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Ready);
}

TEST_CASE("update coordinator: the updated-to moment fires once and is consumed",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    fixture.seedSetting(kKeyUpdatesLastRunVersion, QStringLiteral("0.0.9"));
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    harness.coordinator->start();
    CHECK(harness.coordinator->updatedFromVersion() == QStringLiteral("0.0.9"));
    CHECK(harness.noticeCount == 1);
    // The run is recorded, so the next start is silent.
    CHECK(fixture.readSetting(kKeyUpdatesLastRunVersion).toString() == kCurrent);

    harness.coordinator->acknowledgeUpdated();
    CHECK(harness.coordinator->updatedFromVersion().isEmpty());
}

TEST_CASE("update coordinator: a downgrade between runs passes in silence",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    // A manual reinstall of an older build is not a moment worth announcing.
    fixture.seedSetting(kKeyUpdatesLastRunVersion, QStringLiteral("9.9.9"));
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    harness.coordinator->start();
    CHECK(harness.coordinator->updatedFromVersion().isEmpty());
    CHECK(harness.noticeCount == 0);
}

TEST_CASE("update coordinator: restart-now only arms a flag", "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);

    CHECK_FALSE(harness.coordinator->pendingRestart());
    harness.coordinator->armPendingRestart();
    // The spawn happens in the aboutToQuit hook, never here, so every existing
    // QML close guard runs first and cancelling one cancels the restart.
    CHECK(harness.coordinator->pendingRestart());
    CHECK(harness.staging->promotes() == 0);
}

TEST_CASE("update coordinator: turning checks off mid-flight disables and aborts",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    harness.manifest->setResult(
        ManifestFetchResult::ok(manifest(QStringLiteral("0.2.0")), QByteArray()));
    harness.download->setMode(FakeDownloadGateway::Mode::Hold);

    harness.coordinator->checkNow();
    REQUIRE(harness.coordinator->snapshot().phase == UpdatePhase::Downloading);

    prefs->setChecksEnabled(false);
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Disabled);
    CHECK(harness.download->aborts() == 1);
    // Nothing new is scheduled while disabled: a later CheckRequested from an
    // already-armed timer is a no-op in the reducer.
    harness.coordinator->firePendingCheck();
    CHECK(harness.manifest->fetches() == 1);
    CHECK(harness.coordinator->snapshot().phase == UpdatePhase::Disabled);
}

TEST_CASE("update coordinator: a lastCheck that was never written is invalid",
          "[update][coordinator]") {
    Fixture fixture;
    REQUIRE(fixture.valid());
    const auto prefs = fixture.makePrefs();
    Harness harness;
    harness.build(prefs.get(), kNow);
    CHECK_FALSE(harness.coordinator->lastCheck().isValid());
}
