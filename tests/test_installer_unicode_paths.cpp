// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The REAL Win32FileOps over a tree whose components are Cyrillic, CJK and
// spaces and whose full path is past MAX_PATH. This is the suite that would
// have caught a missing \\?\ prefix: every one of these operations silently
// fails at 260 characters without it, and a Windows account name is very often
// non-ASCII.
//
// Everything lives inside a QTemporaryDir and is torn down through the ops
// under test (removeTreeBestEffort), which doubles as the assertion that the
// uninstaller can delete what the installer wrote.

#include "installer/ops/Win32FileOps.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>

using dish::installer::OpResult;
using dish::installer::removeTreeBestEffort;
using dish::installer::SetupError;
using dish::installer::toExtendedPath;
using dish::installer::Win32FileOps;

namespace {

// Cyrillic + CJK + a space + an ASCII tail: the four shapes that break a
// narrow-char or MAX_PATH-bound implementation.
const QString kUnicodeSegment =
    QStringLiteral("\u0414\u0438\u0448 \u30C7\u30A3\u30C3\u30B7\u30E5 dir");

// Repeats the segment until the absolute path is comfortably past MAX_PATH.
QString deepDirUnder(const QString& root) {
    QString path = root;
    while (path.size() < 300) { path += QLatin1Char('/') + kUnicodeSegment; }
    return path;
}

QByteArray payloadBytes() {
    QByteArray bytes;
    bytes.reserve(300000);
    for (int i = 0; i < 300000; ++i) { bytes.append(static_cast<char>('A' + (i % 26))); }
    return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return file.write(bytes) == bytes.size();
}

QByteArray sha256HexOf(const QByteArray& bytes) {
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex();
}

} // namespace

TEST_CASE("installer unicode paths: the extended-length prefix form", "[installer][win32-files]") {
    CHECK(toExtendedPath(QStringLiteral("C:/App/dish.exe")) ==
          QStringLiteral("\\\\?\\C:\\App\\dish.exe"));
    CHECK(toExtendedPath(QStringLiteral("C:\\App\\dish.exe")) ==
          QStringLiteral("\\\\?\\C:\\App\\dish.exe"));
    CHECK(toExtendedPath(QStringLiteral("C:/App/./sub/../dish.exe")) ==
          QStringLiteral("\\\\?\\C:\\App\\dish.exe"));
    // UNC gets the documented \\?\UNC\ shape, not \\?\\\server.
    CHECK(toExtendedPath(QStringLiteral("//server/share/App")) ==
          QStringLiteral("\\\\?\\UNC\\server\\share\\App"));
    // Already prefixed passes through untouched.
    CHECK(toExtendedPath(QStringLiteral("\\\\?\\C:\\App")) == QStringLiteral("\\\\?\\C:\\App"));
}

TEST_CASE("installer unicode paths: copy, verify, rename and delete past MAX_PATH",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    Win32FileOps ops;

    const QString deep = deepDirUnder(root);
    REQUIRE(deep.size() > 260);
    const OpResult made = ops.ensureDir(deep);
    INFO("deep dir: " << deep.toStdString());
    REQUIRE(made.ok);
    CHECK(ops.exists(deep));
    // Nested creation is recursive: every missing ancestor was made.
    CHECK(ops.exists(root + QLatin1Char('/') + kUnicodeSegment));

    const QByteArray payload = payloadBytes();
    const QString source = root + QStringLiteral("/source \u0444\u0430\u0439\u043B.bin");
    REQUIRE(writeFile(source, payload));

    const QString dest = deep + QStringLiteral("/dish \u0444\u0430\u0439\u043B.exe");
    qint64 reported = 0;
    const OpResult copied = ops.copyWithProgress(source, dest, [&reported](qint64 delta) {
        reported += delta;
        return true;
    });
    REQUIRE(copied.ok);
    CHECK(copied.path == dest);
    CHECK(ops.exists(dest));
    // Progress is byte deltas, never more than the file itself.
    CHECK(reported <= payload.size());

    const QByteArray hex = sha256HexOf(payload);
    CHECK(ops.verifySha256(dest, hex).ok);
    // The expected hash is lowercased before the compare, so an uppercase
    // manifest value still verifies.
    CHECK(ops.verifySha256(dest, hex.toUpper()).ok);

    const OpResult mismatch = ops.verifySha256(dest, QByteArray(64, 'a'));
    CHECK_FALSE(mismatch.ok);
    CHECK(mismatch.error == SetupError::PayloadCorrupt);

    const OpResult missing = ops.verifySha256(deep + QStringLiteral("/nope.bin"), hex);
    CHECK_FALSE(missing.ok);
    CHECK(missing.error == SetupError::FileOpFailed);

    // The upgrade commit is renames, and they have to work at this depth too.
    const QString renamed = deep + QStringLiteral("/dish.exe.old");
    REQUIRE(ops.rename(dest, renamed).ok);
    CHECK_FALSE(ops.exists(dest));
    CHECK(ops.exists(renamed));
    REQUIRE(ops.rename(renamed, dest).ok);

    const QStringList listed = ops.listRecursive(root);
    CHECK(listed.contains(dest));
    CHECK(listed.contains(source));
    // Sorted and forward-slashed, so a caller can compare against manifest
    // paths without re-normalizing.
    QStringList sorted = listed;
    sorted.sort();
    CHECK(listed == sorted);

    REQUIRE(ops.remove(dest).ok);
    CHECK_FALSE(ops.exists(dest));
    // Removing what is already gone is the desired state, not an error.
    CHECK(ops.remove(dest).ok);

    CHECK(removeTreeBestEffort(ops, root + QLatin1Char('/') + kUnicodeSegment).ok);
    CHECK_FALSE(ops.exists(deep));
    CHECK(ops.remove(source).ok);
}

TEST_CASE("installer unicode paths: a non-empty directory is not removed",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    Win32FileOps ops;

    const QString dir = root + QStringLiteral("/\u4E2D\u6587 folder");
    REQUIRE(ops.ensureDir(dir).ok);
    REQUIRE(writeFile(dir + QStringLiteral("/keep.txt"), QByteArray("x")));

    const OpResult refused = ops.removeDirIfEmpty(dir);
    CHECK_FALSE(refused.ok);
    CHECK(refused.win32 == 145u); // ERROR_DIR_NOT_EMPTY
    CHECK(ops.exists(dir));

    REQUIRE(ops.remove(dir + QStringLiteral("/keep.txt")).ok);
    CHECK(ops.removeDirIfEmpty(dir).ok);
    CHECK_FALSE(ops.exists(dir));
    // Idempotent: a directory that is already gone reports success.
    CHECK(ops.removeDirIfEmpty(dir).ok);
}

TEST_CASE("installer unicode paths: free space answers for a destination that does not exist yet",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    Win32FileOps ops;

    const qint64 existing = ops.freeBytesFor(root);
    CHECK(existing > 0);
    // The preflight runs BEFORE the install dir is created, so the answer has
    // to describe the volume the install will land on.
    const qint64 planned = ops.freeBytesFor(deepDirUnder(root) + QStringLiteral("/not/created"));
    CHECK(planned > 0);
    CHECK(ops.freeBytesFor(QStringLiteral("Z:/no/such/volume")) == -1);
}

TEST_CASE("installer unicode paths: a copy whose progress callback cancels is typed Cancelled",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    Win32FileOps ops;

    const QString source = root + QStringLiteral("/big.bin");
    REQUIRE(writeFile(source, payloadBytes()));

    const QString dest = root + QStringLiteral("/copy.bin");
    const OpResult cancelled = ops.copyWithProgress(source, dest, [](qint64) { return false; });
    CHECK_FALSE(cancelled.ok);
    CHECK(cancelled.error == SetupError::Cancelled);
    CHECK(cancelled.path == dest);
}

TEST_CASE("installer unicode paths: copying a missing source fails typed",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    Win32FileOps ops;

    const OpResult result =
        ops.copyWithProgress(root + QStringLiteral("/missing.bin"),
                             root + QStringLiteral("/dest.bin"), [](qint64) { return true; });
    CHECK_FALSE(result.ok);
    CHECK(result.error == SetupError::FileOpFailed);
    CHECK(result.win32 != 0u); // the raw Win32 code travels for the log
}

TEST_CASE("installer unicode paths: listRecursive walks subdirectories and returns files only",
          "[installer][win32-files]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/image");
    Win32FileOps ops;

    REQUIRE(ops.ensureDir(root + QStringLiteral("/licenses")).ok);
    REQUIRE(ops.ensureDir(root + QStringLiteral("/platforms")).ok);
    REQUIRE(writeFile(root + QStringLiteral("/dish.exe"), QByteArray("MZ")));
    REQUIRE(writeFile(root + QStringLiteral("/licenses/LICENSE.txt"), QByteArray("text")));

    const QStringList files = ops.listRecursive(root);
    CHECK(files.size() == 2);
    CHECK(files.contains(root + QStringLiteral("/dish.exe")));
    CHECK(files.contains(root + QStringLiteral("/licenses/LICENSE.txt")));
    // Directories are not files: an empty one is absent from the list, and the
    // uninstaller prunes it separately.
    CHECK_FALSE(files.contains(root + QStringLiteral("/platforms")));

    CHECK(removeTreeBestEffort(ops, root).ok);
    CHECK_FALSE(ops.exists(root));
}
