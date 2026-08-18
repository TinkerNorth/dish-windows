// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The boot gate: every guard on its own, the attempt counter that bounds a
// failed apply at two boots, and the two skips (the --no-update-handoff loop
// breaker and the running-instance probe) that must NOT consume an attempt.
//
// Sandboxing. LOCALAPPDATA is redirected at a temp directory, so the staging
// root is a fixture and never the developer's real %LOCALAPPDATA%\Dish\updates.
// The settings cannot be redirected the same way: the boot gate names its hive
// explicitly with QSettings(organization, application), and that constructor
// always uses NativeFormat — the Windows registry — no matter what
// setDefaultFormat() says. So these cases DO write the updater's own values
// under HKCU\Software\TinkerNorth\Dish, and the fixture snapshots every one of
// them on entry (clearing them so each case starts from a known state) and puts
// them back on exit, removing the ones that did not exist. Nothing else in that
// key is read or written.
//
// CI runs `ctest --parallel` and Catch2 registers one PROCESS per case, so every
// case that borrows that machine-wide state takes a named mutex first and holds
// it for the whole fixture.

#include "update/UpdateHandoff.h"

#include "core/reducer/UpdateMachine.h"
#include "source/store/UpdatePreferenceStore.h"
#include "update/FileStagingStore.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QSettings>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <QThread>
#include <QVariant>

// Pulls windows.h, so it stays last: no Win32 macro may reach the Qt headers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

using dish::reducer::kMaxApplyAttemptsPerVersion;
using dish::source::kKeyUpdatesHandoffAttempts;
using dish::source::kKeyUpdatesHandoffVersion;
using dish::source::UpdatePreferenceStore;
using dish::update::kApplyAttemptsName;
using dish::update::kReadyMarkerName;
using dish::update::kSetupExeName;
using dish::update::ReadyMarker;
using dish::update::RunningInstanceMutex;
using dish::update::serializeReadyMarker;
using dish::update::StagedUpdate;
using dish::update::UpdateHandoff;

namespace {

// DISH_VERSION is what the gate compares against; the fixtures stage far above
// it so the "strictly newer" guard is never the accidental reason a case fails.
const QString kCurrent = QString::fromLatin1(DISH_VERSION);
const QByteArray kSetupBytes = QByteArray("pretend staged installer, not a PE image");

// A real, harmless executable to stand in for a staged dish-setup.exe when a
// case actually lets the gate SPAWN. It matters that it is a valid PE: Windows
// answers CreateProcessW on a non-PE file with a MODAL "Unsupported 16-Bit
// Application" hard-error dialog, which would hang the suite rather than fail
// the call. (The product cannot reach that path: the gate re-hashes the staged
// exe against its marker first.) PING.EXE rejects the installer's arguments and
// exits immediately, so nothing is left running.
QString systemPingExe() {
    const QString root = QDir::fromNativeSeparators(qEnvironmentVariable("SystemRoot"));
    if (root.isEmpty()) { return {}; }
    return root + QStringLiteral("/System32/PING.EXE");
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    return file.readAll();
}

QString shaOf(const QByteArray& bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return file.write(bytes) == bytes.size();
}

// The values the code under test may write. Snapshotted and restored so a
// developer's live preferences survive a run of this suite.
const char* const kTouchedKeys[] = {
    kKeyUpdatesHandoffVersion,
    kKeyUpdatesHandoffAttempts,
    UpdatePreferenceStore::kKeySkippedVersion,
};

// Serializes the cases that touch machine-wide state across the separate
// processes ctest launches for them. WAIT_ABANDONED (a previous holder was
// killed) still grants ownership, which is what keeps one crashed case from
// wedging the rest of the run.
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

class Sandbox {
  public:
    Sandbox() {
        savedLocalAppData_ = qgetenv("LOCALAPPDATA");
        QDir().mkpath(root() + QStringLiteral("/local"));
        qputenv("LOCALAPPDATA",
                QDir::toNativeSeparators(root() + QStringLiteral("/local")).toUtf8());

        QSettings settings(QStringLiteral("TinkerNorth"), QStringLiteral("Dish"));
        for (const char* key : kTouchedKeys) {
            const QString name = QLatin1String(key);
            saved_.insert(name, settings.value(name));
            settings.remove(name);
        }
        settings.sync();
    }

    ~Sandbox() {
        QSettings settings(QStringLiteral("TinkerNorth"), QStringLiteral("Dish"));
        for (auto it = saved_.cbegin(); it != saved_.cend(); ++it) {
            if (it.value().isValid()) {
                settings.setValue(it.key(), it.value());
            } else {
                settings.remove(it.key());
            }
        }
        settings.sync();

        if (savedLocalAppData_.isEmpty()) {
            qunsetenv("LOCALAPPDATA");
        } else {
            qputenv("LOCALAPPDATA", savedLocalAppData_);
        }
    }

    Sandbox(const Sandbox&) = delete;
    Sandbox& operator=(const Sandbox&) = delete;

    bool valid() const { return dir_.isValid(); }
    QString root() const { return QDir::fromNativeSeparators(dir_.path()); }
    QString updatesRoot() const { return root() + QStringLiteral("/local/Dish/updates"); }
    QString readyDirFor(const QString& version) const {
        return updatesRoot() + QStringLiteral("/ready/") + version;
    }

    // The settings the boot gate reaches by naming the hive explicitly.
    QSettings settings() const {
        return QSettings(QStringLiteral("TinkerNorth"), QStringLiteral("Dish"));
    }

    // A complete ready\<version> tree; `bytes` is what the exe contains and
    // `markerSha` / `markerSize` default to describing it truthfully.
    bool plant(const QString& version, const QByteArray& bytes = kSetupBytes,
               const QString& markerSha = QString(), qint64 markerSize = -1) const {
        const QString dir = readyDirFor(version);
        if (!writeFile(dir + QLatin1Char('/') + QLatin1String(kSetupExeName), bytes)) {
            return false;
        }
        ReadyMarker marker;
        marker.schema = 1;
        marker.version = version;
        marker.sha256 = markerSha.isEmpty() ? shaOf(bytes) : markerSha;
        marker.size = markerSize < 0 ? bytes.size() : markerSize;
        marker.stagedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        return writeFile(dir + QLatin1Char('/') + QLatin1String(kReadyMarkerName),
                         serializeReadyMarker(marker));
    }

    void setAttempts(const QString& version, int count) const {
        QSettings s = settings();
        s.setValue(QLatin1String(kKeyUpdatesHandoffVersion), version);
        s.setValue(QLatin1String(kKeyUpdatesHandoffAttempts), count);
        s.sync();
    }

    int attempts() const {
        QSettings s = settings();
        return s.value(QLatin1String(kKeyUpdatesHandoffAttempts), 0).toInt();
    }

    QString recordedVersion() const {
        QSettings s = settings();
        return s.value(QLatin1String(kKeyUpdatesHandoffVersion)).toString();
    }

    QString skippedVersion() const {
        QSettings s = settings();
        return s.value(QLatin1String(UpdatePreferenceStore::kKeySkippedVersion)).toString();
    }

  private:
    // First member: acquired before anything is borrowed, released last.
    MachineStateLock lock_;
    QTemporaryDir dir_;
    QByteArray savedLocalAppData_;
    QMap<QString, QVariant> saved_;
};

} // namespace

TEST_CASE("update handoff: the hive, the mutex and the flag are named exactly once",
          "[update][handoff]") {
    CHECK(QString::fromLatin1(dish::update::kSettingsOrganization) ==
          QStringLiteral("TinkerNorth"));
    CHECK(QString::fromLatin1(dish::update::kSettingsApplication) == QStringLiteral("Dish"));
    CHECK(QString::fromWCharArray(UpdateHandoff::kRunningMutexName) ==
          QStringLiteral("Local\\TinkerNorth.Dish.Running"));
    CHECK(QString::fromLatin1(UpdateHandoff::kNoHandoffFlag) ==
          QStringLiteral("--no-update-handoff"));
}

TEST_CASE("update handoff: the running executable resolves", "[update][handoff]") {
    const QString path = UpdateHandoff::runningExecutablePath();
    REQUIRE_FALSE(path.isEmpty());
    CHECK(QFileInfo::exists(path));
    CHECK(path.contains(QLatin1Char('/'))); // forward slashes, like every Qt path here
    CHECK(path.endsWith(QStringLiteral("DishTests.exe"), Qt::CaseInsensitive));
}

TEST_CASE("update handoff: the instance probe sees a held mutex and nothing else",
          "[update][handoff]") {
    // The probe is machine-wide, so this case has to own the lock too.
    MachineStateLock lock;
    if (UpdateHandoff::anotherInstanceRunning()) {
        SUCCEED("skipped: another Dish instance holds the running-instance mutex");
        return;
    }
    CHECK_FALSE(UpdateHandoff::anotherInstanceRunning());
    {
        RunningInstanceMutex held;
        CHECK(held.held());
        // A second instance's gate must find it: that is what keeps two Dish
        // processes from fighting over one apply.
        CHECK(UpdateHandoff::anotherInstanceRunning());
    }
    CHECK_FALSE(UpdateHandoff::anotherInstanceRunning());
}

TEST_CASE("update handoff: nothing staged means nothing to apply", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
}

TEST_CASE("update handoff: a verified stage passes every guard", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));

    const auto staged = UpdateHandoff::verifiedStageForApply(kCurrent);
    REQUIRE(staged.has_value());
    CHECK(staged->version == QStringLiteral("9.9.9"));
    CHECK(staged->dir == sandbox.readyDirFor(QStringLiteral("9.9.9")));
    CHECK(staged->exePath == sandbox.readyDirFor(QStringLiteral("9.9.9")) + QLatin1Char('/') +
                                 QLatin1String(kSetupExeName));
    CHECK(staged->sha256 == shaOf(kSetupBytes));
    CHECK(staged->size == kSetupBytes.size());
}

TEST_CASE("update handoff: a stage that is not strictly newer is discarded", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());

    SECTION("equal to the running build") {
        REQUIRE(sandbox.plant(kCurrent));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
        // Deleted, not merely ignored: a manual upgrade in the meantime must
        // neutralize the stage rather than downgrade the machine.
        CHECK_FALSE(QFileInfo::exists(sandbox.readyDirFor(kCurrent)));
    }
    SECTION("older than the running build") {
        REQUIRE(sandbox.plant(QStringLiteral("0.0.1")));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(QStringLiteral("1.0.0")).has_value());
        CHECK_FALSE(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("0.0.1"))));
    }
}

TEST_CASE("update handoff: a stage whose bytes changed since the promote is discarded",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    // Same size, different content: only the full re-hash catches this, which
    // is why the gate pays for it on the launches that will hand off.
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9"), kSetupBytes, shaOf("something else entirely")));

    CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    CHECK_FALSE(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("9.9.9"))));
}

TEST_CASE("update handoff: an incomplete stage never reaches the hash guard", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());

    SECTION("the recorded size disagrees with the file") {
        REQUIRE(sandbox.plant(QStringLiteral("9.9.9"), kSetupBytes, QString(),
                              kSetupBytes.size() + 10));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    }
    SECTION("the marker is missing (a crash mid-promote)") {
        REQUIRE(writeFile(sandbox.readyDirFor(QStringLiteral("9.9.9")) + QLatin1Char('/') +
                              QLatin1String(kSetupExeName),
                          kSetupBytes));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    }
    SECTION("the marker is torn") {
        REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
        REQUIRE(writeFile(sandbox.readyDirFor(QStringLiteral("9.9.9")) + QLatin1Char('/') +
                              QLatin1String(kReadyMarkerName),
                          "schema=1\nversion=9.9.9\nsha2"));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    }
    SECTION("the exe is gone") {
        REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
        REQUIRE(QFile::remove(sandbox.readyDirFor(QStringLiteral("9.9.9")) + QLatin1Char('/') +
                              QLatin1String(kSetupExeName)));
        CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    }
}

TEST_CASE("update handoff: two failed attempts quarantine the version", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
    sandbox.setAttempts(QStringLiteral("9.9.9"), kMaxApplyAttemptsPerVersion);

    CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
    // Quarantine: the stage is deleted AND the version is muted, so the next
    // check does not re-download a build this machine has twice failed to
    // apply.
    CHECK_FALSE(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("9.9.9"))));
    CHECK(sandbox.skippedVersion() == QStringLiteral("9.9.9"));
}

TEST_CASE("update handoff: an attempt counter recorded for another version does not count",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
    sandbox.setAttempts(QStringLiteral("8.8.8"), kMaxApplyAttemptsPerVersion);

    // The cap is per version, so a burnt 8.8.8 must not block 9.9.9.
    CHECK(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
}

TEST_CASE("update handoff: the attempt is recorded and synced before any spawn",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
    const auto staged = UpdateHandoff::verifiedStageForApply(kCurrent);
    REQUIRE(staged.has_value());

    REQUIRE(UpdateHandoff::recordAttempt(*staged));
    // Read through a SEPARATE QSettings instance: a value that was not synced
    // would not be visible here, and a crash during the spawn would then cost
    // nothing.
    CHECK(sandbox.recordedVersion() == QStringLiteral("9.9.9"));
    CHECK(sandbox.attempts() == 1);
    // The human-readable copy beside the installer, for support.
    const QString attemptsJson = staged->dir + QLatin1Char('/') + QLatin1String(kApplyAttemptsName);
    REQUIRE(QFileInfo::exists(attemptsJson));
    QFile file(attemptsJson);
    REQUIRE(file.open(QIODevice::ReadOnly));
    CHECK(file.readAll().contains("\"count\":1"));

    REQUIRE(UpdateHandoff::recordAttempt(*staged));
    CHECK(sandbox.attempts() == 2);

    // The cap: the third call refuses, and the caller quarantines.
    CHECK_FALSE(UpdateHandoff::recordAttempt(*staged));
    CHECK(sandbox.attempts() == 2);
}

TEST_CASE("update handoff: a new version starts its own attempt count", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
    sandbox.setAttempts(QStringLiteral("8.8.8"), 2);

    const auto staged = UpdateHandoff::verifiedStageForApply(kCurrent);
    REQUIRE(staged.has_value());
    REQUIRE(UpdateHandoff::recordAttempt(*staged));
    CHECK(sandbox.recordedVersion() == QStringLiteral("9.9.9"));
    CHECK(sandbox.attempts() == 1);
}

TEST_CASE("update handoff: quarantine deletes the stage and mutes the version",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));
    const auto staged = UpdateHandoff::verifiedStageForApply(kCurrent);
    REQUIRE(staged.has_value());

    UpdateHandoff::quarantine(*staged);
    CHECK_FALSE(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("9.9.9"))));
    CHECK(sandbox.skippedVersion() == QStringLiteral("9.9.9"));
    // The handoff keys stay: the coordinator reads them once to surface
    // Failed{ApplyFailed} with the manual download link.
    CHECK_FALSE(UpdateHandoff::verifiedStageForApply(kCurrent).has_value());
}

TEST_CASE("update handoff: --no-update-handoff skips without consuming an attempt",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));

    char arg0[] = "dish.exe";
    char flag[] = "--no-update-handoff";
    char* argv[] = {arg0, flag, nullptr};
    CHECK_FALSE(UpdateHandoff::runStartupHandoff(2, argv));

    // Skip-once semantics: nothing persisted, nothing discarded.
    CHECK(sandbox.attempts() == 0);
    CHECK(sandbox.recordedVersion().isEmpty());
    CHECK(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("9.9.9"))));

    // Case-insensitive, and it is found wherever it sits in the tail.
    char other[] = "--some-other-flag";
    char upper[] = "--NO-UPDATE-HANDOFF";
    char* mixed[] = {arg0, other, upper, nullptr};
    CHECK_FALSE(UpdateHandoff::runStartupHandoff(3, mixed));
    CHECK(sandbox.attempts() == 0);
}

TEST_CASE("update handoff: another running instance skips without consuming an attempt",
          "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));

    RunningInstanceMutex held;
    REQUIRE(UpdateHandoff::anotherInstanceRunning());

    char arg0[] = "dish.exe";
    char* argv[] = {arg0, nullptr};
    CHECK_FALSE(UpdateHandoff::runStartupHandoff(1, argv));

    // Nothing was wrong with the stage, so it survives at zero attempts.
    CHECK(sandbox.attempts() == 0);
    CHECK(QFileInfo::exists(sandbox.readyDirFor(QStringLiteral("9.9.9"))));
}

TEST_CASE("update handoff: the gate records the attempt and hands the boot over",
          "[update][handoff]") {
    if (UpdateHandoff::anotherInstanceRunning()) {
        // A real Dish is running on this machine and owns the lifecycle; the
        // gate would skip for that reason instead of the one under test.
        SUCCEED("skipped: another Dish instance holds the running-instance mutex");
        return;
    }
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    const QByteArray realExe = readFile(systemPingExe());
    REQUIRE_FALSE(realExe.isEmpty());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9"), realExe));

    char arg0[] = "dish.exe";
    char* argv[] = {arg0, nullptr};
    // True means main returns 0 immediately; the installer's /OTA mode waits
    // for this process's Running mutex to clear before touching files.
    CHECK(UpdateHandoff::runStartupHandoff(1, argv));

    // Recorded and SYNCED before the spawn, so a crash between the two still
    // costs an attempt and a boot loop stays bounded at two.
    CHECK(sandbox.attempts() == 1);
    CHECK(sandbox.recordedVersion() == QStringLiteral("9.9.9"));

    // The stand-in rejects the installer's arguments and exits on its own.
    QThread::msleep(500);
}

TEST_CASE("update handoff: a spawn that cannot start leaves startup alone", "[update][handoff]") {
    Sandbox sandbox;
    REQUIRE(sandbox.valid());
    REQUIRE(sandbox.plant(QStringLiteral("9.9.9")));

    StagedUpdate missing;
    missing.version = QStringLiteral("9.9.9");
    missing.dir = sandbox.readyDirFor(QStringLiteral("9.9.9"));
    missing.exePath = missing.dir + QStringLiteral("/not-there-dish-setup.exe");
    missing.sha256 = shaOf(kSetupBytes);
    missing.size = kSetupBytes.size();

    // False is the whole contract: the caller logs it and carries on with a
    // completely normal startup.
    CHECK_FALSE(UpdateHandoff::spawnStagedApply(missing));

    StagedUpdate empty;
    CHECK_FALSE(UpdateHandoff::spawnStagedApply(empty));
}

TEST_CASE("update handoff: the spawn arguments are exactly the documented Inno switches",
          "[update][handoff]") {
    // The consuming half is Inno Setup itself, so what this pins is the
    // PRODUCED shape against docs/INSTALLER.md: the full silent switch set,
    // /OTA (installer.iss [Code] waits on the app's mutex and owns the
    // relaunch duty), and /LOG aimed INTO the stage directory so the log
    // lands where PRIVACY.md says it does. Order matters only for the
    // reader; Inno accepts any, but a stable string is a testable string.
    StagedUpdate staged;
    staged.version = QStringLiteral("9.9.9");
    staged.dir = QStringLiteral("C:/Users/u/AppData/Local/Dish/updates/ready/9.9.9");
    staged.exePath = staged.dir + QStringLiteral("/dish-setup.exe");

    const QString arguments = UpdateHandoff::applyArguments(staged);
    CHECK(arguments ==
          QStringLiteral("/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /OTA "
                         "/LOG=\"C:\\Users\\u\\AppData\\Local\\Dish\\updates\\ready\\9.9.9\\"
                         "apply.log\""));
    // No scope or directory switch, ever: Inno's previous-install record
    // (keyed by the AppId in installer.iss) is the authority, and re-supplying
    // either could fork a machine-scope install into a per-user one.
    CHECK_FALSE(arguments.contains(QStringLiteral("/DIR"), Qt::CaseInsensitive));
    CHECK_FALSE(arguments.contains(QStringLiteral("/CURRENTUSER"), Qt::CaseInsensitive));
    CHECK_FALSE(arguments.contains(QStringLiteral("/ALLUSERS"), Qt::CaseInsensitive));
}
