// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The updater's disk half, against a real temp directory (never
// %LOCALAPPDATA%): the marker-last promote, the janitor's four rules, and the
// hash that is always a full re-read from disk.
//
// The property the whole design rests on is that ready.marker is written LAST,
// so a crash anywhere in a promote leaves a tree that reads as NOT ready and
// gets swept. Several cases here simulate exactly that crash by building the
// half-published tree by hand.

#include "update/FileStagingStore.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

using dish::update::FileStagingStore;
using dish::update::kApplyResultName;
using dish::update::kManifestSnapshotName;
using dish::update::kReadyMarkerName;
using dish::update::kSetupExeName;
using dish::update::parseReadyMarker;
using dish::update::readStagedDir;
using dish::update::ReadyMarker;
using dish::update::serializeReadyMarker;
using dish::update::sha256OfFile;
using dish::update::updatesRootDir;

namespace {

const QByteArray kSetupBytes = QByteArray("MZ pretend installer payload");
// sha256("MZ pretend installer payload"), computed by the same code path the
// production promote uses; pinned as a value in the test that needs it.
const QString kAbcSha =
    QStringLiteral("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

bool writeFile(const QString& path, const QByteArray& bytes) {
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    return file.readAll();
}

QString shaOfBytes(const QString& dir, const QByteArray& bytes) {
    const QString path = dir + QStringLiteral("/hash-probe.bin");
    if (!writeFile(path, bytes)) { return {}; }
    const QString hex = sha256OfFile(path);
    QFile::remove(path);
    return hex;
}

// A complete, valid ready\<version> tree.
bool plantReady(const QString& root, const QString& version, const QByteArray& bytes,
                const QString& sha) {
    const QString dir = root + QStringLiteral("/ready/") + version;
    if (!writeFile(dir + QLatin1Char('/') + QLatin1String(kSetupExeName), bytes)) { return false; }
    ReadyMarker marker;
    marker.schema = 1;
    marker.version = version;
    marker.sha256 = sha;
    marker.size = bytes.size();
    marker.stagedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return writeFile(dir + QLatin1Char('/') + QLatin1String(kReadyMarkerName),
                     serializeReadyMarker(marker));
}

} // namespace

TEST_CASE("update staging store: the layout names are the contract with the installer",
          "[update][staging]") {
    CHECK(QString::fromLatin1(kSetupExeName) == QStringLiteral("dish-setup.exe"));
    CHECK(QString::fromLatin1(kReadyMarkerName) == QStringLiteral("ready.marker"));
    CHECK(QString::fromLatin1(kManifestSnapshotName) == QStringLiteral("manifest.json"));
    CHECK(QString::fromLatin1(kApplyResultName) == QStringLiteral("apply-result.txt"));

    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    FileStagingStore store(root);
    CHECK(store.root() == root);
    CHECK(store.stagingDir() == root + QStringLiteral("/staging"));
    CHECK(store.readyDir() == root + QStringLiteral("/ready"));
    CHECK(store.readyDirFor(QStringLiteral("0.2.0")) == root + QStringLiteral("/ready/0.2.0"));
    CHECK(store.partPathFor(QStringLiteral("0.2.0")) ==
          root + QStringLiteral("/staging/dish-setup-0.2.0.exe.part"));
}

TEST_CASE("update staging store: the updates root hangs off LOCALAPPDATA", "[update][staging]") {
    // Built from the environment variable exactly like the crash handler's
    // directory, NOT from QStandardPaths (which resolves through org/app names
    // the boot gate has not set yet).
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (local.isEmpty()) {
        CHECK(updatesRootDir().isEmpty());
    } else {
        CHECK(updatesRootDir() ==
              QDir::fromNativeSeparators(local) + QStringLiteral("/Dish/updates"));
    }
}

TEST_CASE("update staging store: the marker parses only when it is complete", "[update][staging]") {
    const QByteArray good = "schema=1\nversion=0.2.0\nsha256=" + kAbcSha.toLatin1() +
                            "\nsize=28\nstagedUtc=2026-08-04T10:00:00Z\n";
    const ReadyMarker marker = parseReadyMarker(good);
    REQUIRE(marker.valid);
    CHECK(marker.schema == 1);
    CHECK(marker.version == QStringLiteral("0.2.0"));
    CHECK(marker.sha256 == kAbcSha);
    CHECK(marker.size == 28);
    CHECK(marker.stagedUtc == QStringLiteral("2026-08-04T10:00:00Z"));

    // Additive-only: an older client still reads a newer marker.
    QByteArray extra = good;
    extra.append("futureKey=whatever\n");
    CHECK(parseReadyMarker(extra).valid);

    SECTION("a missing required field invalidates the whole marker") {
        for (const char* drop : {"schema=1\n", "version=0.2.0\n", "size=28\n"}) {
            QByteArray broken = good;
            broken.replace(drop, "");
            INFO("dropped: " << drop);
            CHECK_FALSE(parseReadyMarker(broken).valid);
        }
        QByteArray noSha = good;
        noSha.replace("sha256=" + kAbcSha.toLatin1() + "\n", "");
        CHECK_FALSE(parseReadyMarker(noSha).valid);
    }
    SECTION("a torn write is indistinguishable from a missing one") {
        // The tail a crash leaves: a line with no '=' yet.
        CHECK_FALSE(parseReadyMarker("schema=1\nversion=0.2.0\nsha2").valid);
        CHECK_FALSE(parseReadyMarker(QByteArray()).valid);
        CHECK_FALSE(parseReadyMarker("garbage").valid);
    }
    SECTION("field values are validated, not just present") {
        QByteArray badSchema = good;
        badSchema.replace("schema=1", "schema=2");
        CHECK_FALSE(parseReadyMarker(badSchema).valid);

        QByteArray badVersion = good;
        badVersion.replace("version=0.2.0", "version=v0.2.0");
        CHECK_FALSE(parseReadyMarker(badVersion).valid);

        QByteArray shortSha = good;
        shortSha.replace(kAbcSha.toLatin1(), QByteArray(63, 'a'));
        CHECK_FALSE(parseReadyMarker(shortSha).valid);

        QByteArray zeroSize = good;
        zeroSize.replace("size=28", "size=0");
        CHECK_FALSE(parseReadyMarker(zeroSize).valid);
    }
}

TEST_CASE("update staging store: the marker round-trips through its serializer",
          "[update][staging]") {
    ReadyMarker marker;
    marker.schema = 1;
    marker.version = QStringLiteral("1.2.3");
    marker.sha256 = kAbcSha;
    marker.size = 4096;
    marker.stagedUtc = QStringLiteral("2026-08-04T10:00:00Z");

    const QByteArray text = serializeReadyMarker(marker);
    CHECK(text.startsWith("schema=1\n")); // schema first, so an old parser bails early
    CHECK(text.endsWith('\n'));
    const ReadyMarker parsed = parseReadyMarker(text);
    REQUIRE(parsed.valid);
    CHECK(parsed.version == marker.version);
    CHECK(parsed.sha256 == marker.sha256);
    CHECK(parsed.size == marker.size);
    CHECK(parsed.stagedUtc == marker.stagedUtc);
}

TEST_CASE("update staging store: sha256OfFile is a full read of the bytes on disk",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());

    // Known vector: sha256("abc").
    CHECK(shaOfBytes(root, QByteArray("abc")) == kAbcSha);
    // Empty file.
    CHECK(shaOfBytes(root, QByteArray()) ==
          QStringLiteral("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    // A file that is not there hashes to nothing rather than to a lie.
    CHECK(sha256OfFile(root + QStringLiteral("/missing.bin")).isEmpty());
}

TEST_CASE("update staging store: a promote publishes the exe, the snapshot and the marker last",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    FileStagingStore store(root);

    const QString version = QStringLiteral("0.2.0");
    const QString part = store.partPathFor(version);
    REQUIRE(writeFile(part, kSetupBytes));
    const QString sha = sha256OfFile(part);
    REQUIRE_FALSE(sha.isEmpty());

    const QByteArray manifestBytes = "{\"schema\":1,\"version\":\"0.2.0\"}";
    const auto readyDir = store.promote(version, sha, kSetupBytes.size(), manifestBytes);
    REQUIRE(readyDir.has_value());
    CHECK(*readyDir == store.readyDirFor(version));

    const QString exePath = *readyDir + QLatin1Char('/') + QLatin1String(kSetupExeName);
    CHECK(readFile(exePath) == kSetupBytes);
    // The .part is MOVED, not copied: no second 40 MB left behind.
    CHECK_FALSE(QFileInfo::exists(part));
    CHECK(readFile(*readyDir + QLatin1Char('/') + QLatin1String(kManifestSnapshotName)) ==
          manifestBytes);

    const ReadyMarker marker =
        parseReadyMarker(readFile(*readyDir + QLatin1Char('/') + QLatin1String(kReadyMarkerName)));
    REQUIRE(marker.valid);
    CHECK(marker.version == version);
    CHECK(marker.sha256 == sha);
    CHECK(marker.size == kSetupBytes.size());

    const auto staged = store.findStaged();
    REQUIRE(staged.has_value());
    CHECK(staged->version == version);
    CHECK(staged->dir == *readyDir);
    CHECK(staged->exePath == exePath);
    CHECK(staged->sha256 == sha);
    CHECK(staged->size == kSetupBytes.size());
}

TEST_CASE("update staging store: a promote refuses bytes that disagree with the manifest",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    FileStagingStore store(root);
    const QString version = QStringLiteral("0.2.0");
    const QString part = store.partPathFor(version);

    SECTION("a hash mismatch drops the bytes entirely") {
        REQUIRE(writeFile(part, kSetupBytes));
        CHECK_FALSE(store.promote(version, QString(64, QLatin1Char('a')), kSetupBytes.size(), {})
                        .has_value());
        // No resume: the next cycle downloads from zero.
        CHECK_FALSE(QFileInfo::exists(part));
        CHECK_FALSE(QFileInfo::exists(store.readyDirFor(version)));
    }
    SECTION("a size mismatch is refused before the hash is even computed") {
        REQUIRE(writeFile(part, kSetupBytes));
        CHECK_FALSE(
            store.promote(version, sha256OfFile(part), kSetupBytes.size() + 1, {}).has_value());
        CHECK_FALSE(QFileInfo::exists(store.readyDirFor(version)));
    }
    SECTION("a missing part is not a promote") {
        CHECK_FALSE(store.promote(version, QString(64, QLatin1Char('a')), 10, {}).has_value());
    }
    SECTION("a version that is not a version is refused") {
        REQUIRE(writeFile(store.partPathFor(QStringLiteral("banana")), kSetupBytes));
        CHECK_FALSE(store.promote(QStringLiteral("banana"), sha256OfFile(part), 1, {}).has_value());
    }
}

TEST_CASE("update staging store: a crash between the exe and the marker leaves a sweepable tree",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    FileStagingStore store(root);

    const QString dir = store.readyDirFor(QStringLiteral("0.2.0"));
    REQUIRE(writeFile(dir + QLatin1Char('/') + QLatin1String(kSetupExeName), kSetupBytes));

    // No marker: the tree reads as not-ready. There is no third state.
    CHECK_FALSE(readStagedDir(dir).has_value());
    CHECK_FALSE(store.findStaged().has_value());

    store.sweep(QStringLiteral("0.1.0"));
    CHECK_FALSE(QFileInfo::exists(dir));
}

TEST_CASE("update staging store: a ready dir is validated against its own name and size",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString sha = shaOfBytes(root, kSetupBytes);

    SECTION("valid") {
        REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
        const auto staged = readStagedDir(root + QStringLiteral("/ready/0.2.0"));
        REQUIRE(staged.has_value());
        CHECK(staged->version == QStringLiteral("0.2.0"));
        CHECK(staged->size == kSetupBytes.size());
    }
    SECTION("the directory name must parse as a version") {
        REQUIRE(plantReady(root, QStringLiteral("latest"), kSetupBytes, sha));
        CHECK_FALSE(readStagedDir(root + QStringLiteral("/ready/latest")).has_value());
    }
    SECTION("the marker must agree with the directory name") {
        REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
        ReadyMarker marker;
        marker.schema = 1;
        marker.version = QStringLiteral("0.3.0"); // renamed dir, stale marker
        marker.sha256 = sha;
        marker.size = kSetupBytes.size();
        REQUIRE(writeFile(root + QStringLiteral("/ready/0.2.0/") + QLatin1String(kReadyMarkerName),
                          serializeReadyMarker(marker)));
        CHECK_FALSE(readStagedDir(root + QStringLiteral("/ready/0.2.0")).has_value());
    }
    SECTION("the exe must exist at the recorded size") {
        REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
        REQUIRE(writeFile(root + QStringLiteral("/ready/0.2.0/") + QLatin1String(kSetupExeName),
                          kSetupBytes + "extra"));
        CHECK_FALSE(readStagedDir(root + QStringLiteral("/ready/0.2.0")).has_value());
    }
    SECTION("a missing exe is not a stage") {
        REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
        REQUIRE(
            QFile::remove(root + QStringLiteral("/ready/0.2.0/") + QLatin1String(kSetupExeName)));
        CHECK_FALSE(readStagedDir(root + QStringLiteral("/ready/0.2.0")).has_value());
    }
}

TEST_CASE("update staging store: findStaged keeps the highest valid version", "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString sha = shaOfBytes(root, kSetupBytes);
    FileStagingStore store(root);

    REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
    REQUIRE(plantReady(root, QStringLiteral("0.10.0"), kSetupBytes, sha));
    REQUIRE(plantReady(root, QStringLiteral("0.3.0"), kSetupBytes, sha));
    // Junk next to them must not win, and must not break the scan.
    REQUIRE(QDir().mkpath(root + QStringLiteral("/ready/banana")));

    const auto staged = store.findStaged();
    REQUIRE(staged.has_value());
    CHECK(staged->version == QStringLiteral("0.10.0")); // triple order, not text order
}

TEST_CASE("update staging store: the janitor's four rules", "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString sha = shaOfBytes(root, kSetupBytes);
    FileStagingStore store(root);

    // 1. Anything at or below the running version (the post-apply loop breaker
    //    and the answer to "the installer cannot delete its own directory").
    REQUIRE(plantReady(root, QStringLiteral("0.1.0"), kSetupBytes, sha));
    REQUIRE(plantReady(root, QStringLiteral("0.0.9"), kSetupBytes, sha));
    // 2. Junk names and 3. incomplete trees.
    REQUIRE(QDir().mkpath(root + QStringLiteral("/ready/banana")));
    REQUIRE(writeFile(root + QStringLiteral("/ready/0.5.0/") + QLatin1String(kSetupExeName),
                      kSetupBytes));
    // 4. Several valid survivors: only the highest is kept.
    REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
    REQUIRE(plantReady(root, QStringLiteral("0.3.0"), kSetupBytes, sha));

    store.sweep(QStringLiteral("0.1.0"));

    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/ready/0.1.0")));
    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/ready/0.0.9")));
    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/ready/banana")));
    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/ready/0.5.0")));
    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/ready/0.2.0")));
    CHECK(QFileInfo::exists(root + QStringLiteral("/ready/0.3.0")));

    const auto staged = store.findStaged();
    REQUIRE(staged.has_value());
    CHECK(staged->version == QStringLiteral("0.3.0"));
}

TEST_CASE("update staging store: the janitor sweeps stale partials only", "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    FileStagingStore store(root);

    const QString fresh = store.partPathFor(QStringLiteral("0.3.0"));
    const QString stale = store.partPathFor(QStringLiteral("0.2.0"));
    REQUIRE(writeFile(fresh, kSetupBytes));
    REQUIRE(writeFile(stale, kSetupBytes));
    {
        // 25 hours old: past the 24 h cutoff. A younger partial may belong to a
        // download running right now, so age is the only safe discriminator.
        QFile file(stale);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(-25 * 3600),
                                 QFileDevice::FileModificationTime));
    }

    store.sweep(QStringLiteral("0.1.0"));
    CHECK(QFileInfo::exists(fresh));
    CHECK_FALSE(QFileInfo::exists(stale));
}

TEST_CASE("update staging store: discard removes the ready tree and the partial",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString sha = shaOfBytes(root, kSetupBytes);
    FileStagingStore store(root);

    REQUIRE(plantReady(root, QStringLiteral("0.2.0"), kSetupBytes, sha));
    REQUIRE(writeFile(store.partPathFor(QStringLiteral("0.2.0")), kSetupBytes));

    store.discard(QStringLiteral("0.2.0"));
    CHECK_FALSE(QFileInfo::exists(store.readyDirFor(QStringLiteral("0.2.0"))));
    CHECK_FALSE(QFileInfo::exists(store.partPathFor(QStringLiteral("0.2.0"))));
    CHECK_FALSE(store.findStaged().has_value());

    // Discarding what is not there is a no-op, not a failure.
    store.discard(QStringLiteral("9.9.9"));
    store.discard(QString());
}

TEST_CASE("update staging store: the disk preflight demands the asset plus headroom",
          "[update][staging]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    FileStagingStore store(QDir::fromNativeSeparators(temp.path()));
    CHECK(store.hasRoomFor(1024));
    // 4 EiB of headroom is not available on any volume.
    CHECK_FALSE(store.hasRoomFor(qint64(1) << 62));
}

TEST_CASE("update staging store: an unset LOCALAPPDATA disables staging instead of guessing",
          "[update][staging]") {
    // Without the variable there is no defensible location, and scattering
    // 40 MB next to the exe (or into the CWD) is worse than not staging.
    struct LocalAppDataGuard {
        QByteArray saved = qgetenv("LOCALAPPDATA");
        LocalAppDataGuard() { qunsetenv("LOCALAPPDATA"); }
        ~LocalAppDataGuard() {
            if (!saved.isEmpty()) { qputenv("LOCALAPPDATA", saved); }
        }
    } guard;

    CHECK(updatesRootDir().isEmpty());
    FileStagingStore store;
    CHECK(store.root().isEmpty());
    CHECK(store.partPathFor(QStringLiteral("0.2.0")).isEmpty());
    CHECK_FALSE(store.findStaged().has_value());
    CHECK_FALSE(store.hasRoomFor(1));
    CHECK_FALSE(store.promote(QStringLiteral("0.2.0"), kAbcSha, 1, {}).has_value());
    // Both janitor entry points are no-ops rather than crashes.
    store.discard(QStringLiteral("0.2.0"));
    store.sweep(QStringLiteral("0.1.0"));
}
