// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The [stub PE][payload.zip][32-byte trailer] artifact, end to end: a fixture
// image is zipped with the same miniz writer the pack tool uses, welded onto a
// stand-in stub with appendZipAndTrailer(), and then read back exactly the way
// dish-setup.exe reads itself at runtime — trailer, whole-overlay CRC, extract.
//
// The pack TOOL binary is not driven here on purpose: DishTests compiles
// PayloadFormat.cpp and miniz per-target (tests/CMakeLists.txt), so this
// exercises the identical code path without depending on a target that is
// outside ALL. What dish-payload-pack adds on top (manifest.json emission, the
// uninstall.exe alias, the size report) is covered by the manifest suite and by
// scripts/test-installer-roundtrip.ps1.

#include "installer/PayloadFormat.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>
#include <QVector>

#include <atomic>
#include <cstring>
#include <string>

#include "../third_party/miniz/miniz.h"

using dish::installer::payload::appendZipAndTrailer;
using dish::installer::payload::checkOverlayCrc;
using dish::installer::payload::extractAll;
using dish::installer::payload::isSafeEntryName;
using dish::installer::payload::readTrailer;
using dish::installer::payload::Trailer;
using dish::installer::payload::uncompressedTotal;

namespace {

struct ZipItem {
    QByteArray name;
    QByteArray data;
};

// The install image the pack tool would zip: a binary, a nested licence file, a
// zero-byte file and one large highly compressible blob (the Qt DLL stand-in).
QVector<ZipItem> fixtureImage() {
    QVector<ZipItem> items;
    items.append({"dish.exe", QByteArray("MZ fake pe payload")});
    items.append(
        {"licenses/LICENSE.LGPL-3.0.txt", QByteArray("GNU LESSER GENERAL PUBLIC LICENSE")});
    items.append({"empty.marker", QByteArray()});
    items.append({"platforms/qwindows.dll", QByteArray(1024 * 1024, 'Q')});
    items.append({"a/b/c/deep.txt", QByteArray("deep")});
    return items;
}

QByteArray buildZip(const QVector<ZipItem>& items) {
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_writer_init_heap(&zip, 0, 64 * 1024)) { return {}; }
    for (const ZipItem& item : items) {
        if (!mz_zip_writer_add_mem(&zip, item.name.constData(), item.data.constData(),
                                   static_cast<std::size_t>(item.data.size()),
                                   MZ_BEST_COMPRESSION)) {
            mz_zip_writer_end(&zip);
            return {};
        }
    }
    void* buffer = nullptr;
    std::size_t size = 0;
    if (!mz_zip_writer_finalize_heap_archive(&zip, &buffer, &size)) {
        mz_zip_writer_end(&zip);
        return {};
    }
    // Copy before end(): the writer owns (and frees) that heap block.
    const QByteArray bytes(static_cast<const char*>(buffer), static_cast<qsizetype>(size));
    mz_zip_writer_end(&zip);
    return bytes;
}

bool writeFile(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    return file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    return file.readAll();
}

std::wstring wide(const QString& path) { return QDir::toNativeSeparators(path).toStdWString(); }

// The stub bytes a real artifact starts with; any content will do, it only has
// to be there so the payload does not start at offset 0.
const QByteArray kStub = QByteArray(8192, static_cast<char>(0xCC));

// Writes <dir>/dish-setup.exe = [stub][zip][trailer] and returns its path.
QString packInto(const QString& dir, const QVector<ZipItem>& items) {
    const QString exePath = dir + QStringLiteral("/dish-setup.exe");
    const QString zipPath = dir + QStringLiteral("/payload.zip");
    const QByteArray zip = buildZip(items);
    if (zip.isEmpty()) { return {}; }
    if (!writeFile(exePath, kStub)) { return {}; }
    if (!writeFile(zipPath, zip)) { return {}; }
    if (!appendZipAndTrailer(wide(exePath).c_str(), wide(zipPath).c_str())) { return {}; }
    return exePath;
}

struct ProgressTally {
    quint64 bytes = 0;
    int calls = 0;
    int stopAfter = -1; // -1 = never cancel
};

bool tallyProgress(void* ctx, uint64_t delta) {
    auto* tally = static_cast<ProgressTally*>(ctx);
    tally->bytes += delta;
    tally->calls += 1;
    return tally->stopAfter < 0 || tally->calls <= tally->stopAfter;
}

} // namespace

TEST_CASE("installer payload: the trailer is exactly 32 bytes", "[installer][payload]") {
    // On-disk format: any change here breaks every already-shipped artifact.
    CHECK(sizeof(Trailer) == 32);
    CHECK(sizeof(Trailer{}.magic) == 8);
}

TEST_CASE("installer payload: safe entry names", "[installer][payload]") {
    CHECK(isSafeEntryName("dish.exe"));
    CHECK(isSafeEntryName("licenses/LICENSE.LGPL-3.0.txt"));
    CHECK(isSafeEntryName("qml/QtQuick/Controls/Basic/qmldir"));
    CHECK(isSafeEntryName("with space/file name.txt"));
    CHECK(isSafeEntryName("platforms/")); // a directory marker's single trailing slash
}

TEST_CASE("installer payload: hostile entry names are refused", "[installer][payload]") {
    CHECK_FALSE(isSafeEntryName(nullptr));
    CHECK_FALSE(isSafeEntryName(""));
    CHECK_FALSE(isSafeEntryName("../evil.txt"));
    CHECK_FALSE(isSafeEntryName("a/../../evil.txt"));
    CHECK_FALSE(isSafeEntryName("./evil.txt"));
    CHECK_FALSE(isSafeEntryName("/etc/passwd"));
    CHECK_FALSE(isSafeEntryName("C:/Windows/System32/evil.dll"));
    CHECK_FALSE(isSafeEntryName("..\\evil.txt"));
    CHECK_FALSE(isSafeEntryName("dir\\file.txt"));
    CHECK_FALSE(isSafeEntryName("a//b.txt"));
    CHECK_FALSE(isSafeEntryName("stream.txt:hidden"));
    CHECK_FALSE(isSafeEntryName("wild*.txt"));
    CHECK_FALSE(isSafeEntryName("qu?stion.txt"));
    CHECK_FALSE(isSafeEntryName("pipe|.txt"));
    CHECK_FALSE(isSafeEntryName("lt<.txt"));
    CHECK_FALSE(isSafeEntryName("gt>.txt"));
    CHECK_FALSE(isSafeEntryName("quo\"te.txt"));
    CHECK_FALSE(isSafeEntryName("caf\xC3\xA9.txt")); // non-ASCII
    CHECK_FALSE(isSafeEntryName("bell\x07.txt"));
    // Windows strips a trailing dot or space, so the verified name and the
    // created name would differ.
    CHECK_FALSE(isSafeEntryName("trailing."));
    CHECK_FALSE(isSafeEntryName("trailing "));
    CHECK_FALSE(isSafeEntryName("dir./file.txt"));
    // DOS device names resolve to the device wherever a path is used without
    // the \\?\ prefix.
    CHECK_FALSE(isSafeEntryName("CON"));
    CHECK_FALSE(isSafeEntryName("con.txt"));
    CHECK_FALSE(isSafeEntryName("dir/NUL"));
    CHECK_FALSE(isSafeEntryName("PRN.log"));
    CHECK_FALSE(isSafeEntryName("AUX"));
    CHECK_FALSE(isSafeEntryName("COM1"));
    CHECK_FALSE(isSafeEntryName("lpt9.txt"));
    // ... but a name that merely starts like one is fine.
    CHECK(isSafeEntryName("console.txt"));
    CHECK(isSafeEntryName("COM0"));
    CHECK(isSafeEntryName("nullable.dll"));
}

TEST_CASE("installer payload: pack then extract round-trips the whole image",
          "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QVector<ZipItem> items = fixtureImage();
    const QString exePath = packInto(root, items);
    REQUIRE_FALSE(exePath.isEmpty());

    Trailer trailer{};
    REQUIRE(readTrailer(wide(exePath).c_str(), trailer));
    CHECK(std::memcmp(trailer.magic, "DISHSFX1", 8) == 0);
    CHECK(trailer.formatVersion == 1u);
    CHECK(trailer.zipOffset == static_cast<uint64_t>(kStub.size()));
    CHECK(trailer.zipSize ==
          static_cast<uint64_t>(QFileInfo(exePath).size()) - trailer.zipOffset - sizeof(Trailer));
    CHECK(checkOverlayCrc(wide(exePath).c_str(), trailer));

    quint64 total = 0;
    for (const ZipItem& item : items) { total += static_cast<quint64>(item.data.size()); }
    uint64_t uncompressed = 0;
    REQUIRE(uncompressedTotal(wide(exePath).c_str(), trailer, uncompressed));
    CHECK(uncompressed == total);

    const QString dest = root + QStringLiteral("/out");
    ProgressTally tally;
    std::atomic<bool> cancel{false};
    REQUIRE(extractAll(wide(exePath).c_str(), trailer, wide(dest).c_str(), &tallyProgress, &tally,
                       cancel));

    for (const ZipItem& item : items) {
        const QString path = dest + QLatin1Char('/') + QString::fromLatin1(item.name);
        INFO("entry: " << item.name.toStdString());
        REQUIRE(QFileInfo::exists(path));
        CHECK(readFile(path) == item.data);
    }
    // Byte-accurate progress: what the wizard's determinate bar is built on.
    CHECK(tally.bytes == total);
    CHECK(tally.calls > 0);
}

TEST_CASE("installer payload: an artifact is never double-packed", "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString exePath = packInto(root, fixtureImage());
    REQUIRE_FALSE(exePath.isEmpty());
    const qint64 sizeAfterPack = QFileInfo(exePath).size();

    CHECK_FALSE(appendZipAndTrailer(wide(exePath).c_str(),
                                    wide(root + QStringLiteral("/payload.zip")).c_str()));
    CHECK(QFileInfo(exePath).size() == sizeAfterPack);
}

TEST_CASE("installer payload: an unpacked or truncated file has no trailer",
          "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());

    const QString plain = root + QStringLiteral("/plain.exe");
    REQUIRE(writeFile(plain, kStub));
    Trailer trailer{};
    CHECK_FALSE(readTrailer(wide(plain).c_str(), trailer));

    const QString tiny = root + QStringLiteral("/tiny.exe");
    REQUIRE(writeFile(tiny, QByteArray(8, 'x')));
    CHECK_FALSE(readTrailer(wide(tiny).c_str(), trailer));

    const QString exePath = packInto(root, fixtureImage());
    REQUIRE_FALSE(exePath.isEmpty());
    REQUIRE(readTrailer(wide(exePath).c_str(), trailer));
    {
        // One byte off the end: the geometry no longer describes the file.
        QFile file(exePath);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.resize(file.size() - 1));
    }
    Trailer truncated{};
    CHECK_FALSE(readTrailer(wide(exePath).c_str(), truncated));
}

TEST_CASE("installer payload: a corrupted overlay fails the CRC gate", "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString exePath = packInto(root, fixtureImage());
    REQUIRE_FALSE(exePath.isEmpty());

    Trailer trailer{};
    REQUIRE(readTrailer(wide(exePath).c_str(), trailer));
    REQUIRE(checkOverlayCrc(wide(exePath).c_str(), trailer));

    {
        // A single flipped bit inside the payload, the AV-quarantine and
        // half-downloaded shapes this gate exists for.
        QFile file(exePath);
        REQUIRE(file.open(QIODevice::ReadWrite));
        REQUIRE(file.seek(static_cast<qint64>(trailer.zipOffset) + 40));
        char byte = 0;
        REQUIRE(file.read(&byte, 1) == 1);
        byte = static_cast<char>(byte ^ 0x01);
        REQUIRE(file.seek(static_cast<qint64>(trailer.zipOffset) + 40));
        REQUIRE(file.write(&byte, 1) == 1);
    }

    Trailer stillReadable{};
    // The trailer itself is intact: only the payload bytes changed, which is
    // exactly why the whole-overlay CRC is a separate gate.
    CHECK(readTrailer(wide(exePath).c_str(), stillReadable));
    CHECK_FALSE(checkOverlayCrc(wide(exePath).c_str(), trailer));
}

TEST_CASE("installer payload: extraction refuses an escaping entry name", "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());

    QVector<ZipItem> hostile;
    hostile.append({"dish.exe", QByteArray("ok")});
    hostile.append({"../escaped.txt", QByteArray("pwned")});
    const QString exePath = packInto(root, hostile);
    REQUIRE_FALSE(exePath.isEmpty());

    Trailer trailer{};
    REQUIRE(readTrailer(wide(exePath).c_str(), trailer));
    // The overlay is intact: this is a hostile IMAGE, not a corrupt download,
    // and the name gate is what stops it.
    REQUIRE(checkOverlayCrc(wide(exePath).c_str(), trailer));

    const QString dest = root + QStringLiteral("/out");
    std::atomic<bool> cancel{false};
    CHECK_FALSE(
        extractAll(wide(exePath).c_str(), trailer, wide(dest).c_str(), nullptr, nullptr, cancel));
    CHECK_FALSE(QFileInfo::exists(root + QStringLiteral("/escaped.txt")));
    CHECK_FALSE(QFileInfo::exists(dest + QStringLiteral("/../escaped.txt")));
}

TEST_CASE("installer payload: extraction refuses a drive-letter or device entry",
          "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());

    for (const QByteArray& name : {QByteArray("C:/Windows/System32/evil.dll"), QByteArray("CON"),
                                   QByteArray("dir\\traversal.txt")}) {
        QVector<ZipItem> hostile;
        hostile.append({name, QByteArray("pwned")});
        const QString dir = root + QStringLiteral("/case") + QString::number(name.size());
        REQUIRE(QDir().mkpath(dir));
        const QString exePath = packInto(dir, hostile);
        REQUIRE_FALSE(exePath.isEmpty());

        Trailer trailer{};
        REQUIRE(readTrailer(wide(exePath).c_str(), trailer));
        std::atomic<bool> cancel{false};
        INFO("entry: " << name.toStdString());
        CHECK_FALSE(extractAll(wide(exePath).c_str(), trailer,
                               wide(dir + QStringLiteral("/out")).c_str(), nullptr, nullptr,
                               cancel));
    }
}

TEST_CASE("installer payload: a cancelled extraction stops and reports failure",
          "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString root = QDir::fromNativeSeparators(temp.path());
    const QString exePath = packInto(root, fixtureImage());
    REQUIRE_FALSE(exePath.isEmpty());

    Trailer trailer{};
    REQUIRE(readTrailer(wide(exePath).c_str(), trailer));

    SECTION("the cancel flag is polled at every entry boundary") {
        std::atomic<bool> cancel{true};
        const QString dest = root + QStringLiteral("/cancelled");
        CHECK_FALSE(extractAll(wide(exePath).c_str(), trailer, wide(dest).c_str(), nullptr, nullptr,
                               cancel));
        // Stopped before the first entry: nothing was written.
        CHECK_FALSE(QFileInfo::exists(dest + QStringLiteral("/dish.exe")));
    }
    SECTION("a progress callback that returns false aborts mid-entry") {
        ProgressTally tally;
        tally.stopAfter = 1;
        std::atomic<bool> cancel{false};
        CHECK_FALSE(extractAll(wide(exePath).c_str(), trailer,
                               wide(root + QStringLiteral("/aborted")).c_str(), &tallyProgress,
                               &tally, cancel));
        CHECK(tally.calls >= 1);
    }
}

TEST_CASE("installer payload: reading a nonexistent artifact fails cleanly",
          "[installer][payload]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString missing =
        QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/nope/dish-setup.exe");
    Trailer trailer{};
    CHECK_FALSE(readTrailer(wide(missing).c_str(), trailer));
    CHECK_FALSE(readTrailer(nullptr, trailer));
    CHECK_FALSE(checkOverlayCrc(wide(missing).c_str(), trailer));
    std::atomic<bool> cancel{false};
    CHECK_FALSE(extractAll(wide(missing).c_str(), trailer, L"", nullptr, nullptr, cancel));
}
