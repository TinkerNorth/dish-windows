// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The updater's disk half: the staging layout, the marker-last promote, and the
// janitor. Everything here runs on the "dish-update" worker except the free
// functions at the bottom, which the boot gate calls before any thread (or any
// QGuiApplication) exists.
//
// The layout has exactly one committed state and one uncommitted one:
//
//   %LOCALAPPDATA%\Dish\updates\
//     staging\dish-setup-<version>.exe.part   in-flight bytes, never trusted
//     ready\<version>\dish-setup.exe          verified bytes
//     ready\<version>\manifest.json           the latest.json that described them
//     ready\<version>\ready.marker            written LAST; its presence IS the commit
//     ready\<version>\apply-attempts.json     app-written before each spawn
//     ready\<version>\apply-result.txt        installer-written token file
//     ready\<version>\apply.log               installer-written log
//
// Because the marker is written last (tmp + FlushFileBuffers + rename), a crash
// anywhere in the promote leaves a tree that reads as not-ready and is swept.
// There is no third state to reason about, and a torn marker is indistinguishable
// from a missing one to the parser — which is exactly the property wanted.

#pragma once

#include "update/UpdatePorts.h"

#include <QByteArray>
#include <QString>

#include <optional>

namespace dish::update {

inline constexpr const char* kSetupExeName = "dish-setup.exe";
inline constexpr const char* kReadyMarkerName = "ready.marker";
inline constexpr const char* kManifestSnapshotName = "manifest.json";
inline constexpr const char* kApplyAttemptsName = "apply-attempts.json";
inline constexpr const char* kApplyResultName = "apply-result.txt";
inline constexpr const char* kApplyLogName = "apply.log";

// %LOCALAPPDATA%\Dish\updates with forward slashes, built from the environment
// variable exactly like the crash handler's directory (NOT QStandardPaths,
// which resolves through the org/app names the boot gate has not set yet).
// Empty when LOCALAPPDATA is unset: staging is then disabled rather than
// scattering 40 MB next to the exe.
QString updatesRootDir();

// The contents of a ready.marker. `valid` is false for anything the parser
// could not fully account for, including a torn write.
struct ReadyMarker {
    bool valid = false;
    int schema = 0;
    QString version;
    QString sha256;
    qint64 size = 0;
    QString stagedUtc;
};

ReadyMarker parseReadyMarker(const QByteArray& text);
QByteArray serializeReadyMarker(const ReadyMarker& marker);

// Cheap validation of one ready\<version> directory: the name parses as a
// version, the marker is complete and agrees with the directory name, and the
// exe exists at the recorded size. The full sha256 is verified at promote time
// and again by the boot gate; re-hashing 40 MB on every janitor pass would buy
// nothing the boot gate does not already guarantee.
std::optional<StagedUpdate> readStagedDir(const QString& readyDir);

// Lowercase hex of the whole file, read in chunks. "" when the file cannot be
// read. This is the ONLY hash the updater trusts: never the one a stream
// accumulated while writing.
QString sha256OfFile(const QString& path);

class FileStagingStore : public StagingStore {
  public:
    // An empty root means updatesRootDir(); tests pass a temp directory.
    explicit FileStagingStore(QString root = QString());

    QString root() const override { return root_; }
    QString partPathFor(const QString& version) const override;
    std::optional<StagedUpdate> findStaged() override;
    std::optional<QString> promote(const QString& version, const QString& sha256, qint64 size,
                                   const QByteArray& manifestBytes) override;
    void discard(const QString& version) override;
    void sweep(const QString& currentVersion) override;
    bool hasRoomFor(qint64 assetSize) const override;

    QString stagingDir() const;
    QString readyDir() const;
    QString readyDirFor(const QString& version) const;

  private:
    QString root_;
};

} // namespace dish::update
