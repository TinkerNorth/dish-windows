// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "update/FileStagingStore.h"

#include "core/update/UpdateVersion.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>

#include <utility>

// Pulls windows.h, so it stays last: no Win32 macro may reach the Qt headers.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace dish::update {

namespace {

// Antivirus and Explorer both hold brief handles on a freshly written exe, so
// every rename and delete gets five tries over a second before it is deferred
// to the next sweep. Blocking sleeps are fine here: this is the worker thread
// (or the pre-Qt boot gate), never the input path.
constexpr int kRetryCount = 5;
constexpr unsigned long kRetryDelayMs = 250;

template <class Op> bool retrying(Op op) {
    for (int attempt = 0; attempt < kRetryCount; ++attempt) {
        if (op()) { return true; }
        if (attempt + 1 < kRetryCount) { QThread::msleep(kRetryDelayMs); }
    }
    return false;
}

bool removeFileRetrying(const QString& path) {
    return retrying([&path] { return !QFile::exists(path) || QFile::remove(path); });
}

bool removeTreeRetrying(const QString& path) {
    return retrying([&path] {
        QDir dir(path);
        return !dir.exists() || dir.removeRecursively();
    });
}

// Write + FlushFileBuffers + MoveFileEx, so a power loss can leave the target
// absent or complete but never half-written. Used for the ready.marker, which
// is the commit record of the whole promote.
bool writeFileDurably(const QString& path, const QByteArray& bytes) {
    const QString tmpPath = path + QStringLiteral(".tmp");
    const std::wstring tmpNative = QDir::toNativeSeparators(tmpPath).toStdWString();
    const std::wstring finalNative = QDir::toNativeSeparators(path).toStdWString();

    HANDLE handle = ::CreateFileW(tmpNative.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                  FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) { return false; }
    DWORD written = 0;
    const BOOL ok =
        ::WriteFile(handle, bytes.constData(), static_cast<DWORD>(bytes.size()), &written, nullptr);
    const bool complete = ok == TRUE && written == static_cast<DWORD>(bytes.size());
    if (complete) { ::FlushFileBuffers(handle); }
    ::CloseHandle(handle);
    if (!complete) {
        (void)removeFileRetrying(tmpPath);
        return false;
    }
    const bool moved = retrying([&tmpNative, &finalNative] {
        return ::MoveFileExW(tmpNative.c_str(), finalNative.c_str(), MOVEFILE_REPLACE_EXISTING) ==
               TRUE;
    });
    if (!moved) { (void)removeFileRetrying(tmpPath); }
    return moved;
}

bool writeFilePlain(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    const qint64 n = file.write(bytes);
    file.flush();
    file.close();
    return n == bytes.size();
}

QString trimmedValue(const QByteArray& line, qsizetype eq) {
    return QString::fromUtf8(line.mid(eq + 1)).trimmed();
}

} // namespace

QString updatesRootDir() {
    const QString base = qEnvironmentVariable("LOCALAPPDATA");
    if (base.isEmpty()) { return {}; }
    return QDir::fromNativeSeparators(base) + QStringLiteral("/Dish/updates");
}

ReadyMarker parseReadyMarker(const QByteArray& text) {
    ReadyMarker marker;
    bool sawSchema = false;
    bool sawVersion = false;
    bool sawSha = false;
    bool sawSize = false;
    for (const QByteArray& raw : text.split('\n')) {
        const QByteArray line = raw.trimmed();
        if (line.isEmpty()) { continue; }
        const qsizetype eq = line.indexOf('=');
        if (eq <= 0) { return {}; }
        const QByteArray key = line.left(eq).trimmed();
        const QString value = trimmedValue(line, eq);
        if (key == "schema") {
            bool ok = false;
            marker.schema = value.toInt(&ok);
            sawSchema = ok;
        } else if (key == "version") {
            marker.version = value;
            sawVersion = isValidVersion(value);
        } else if (key == "sha256") {
            marker.sha256 = value;
            sawSha = value.size() == 64;
        } else if (key == "size") {
            bool ok = false;
            marker.size = value.toLongLong(&ok);
            sawSize = ok && marker.size > 0;
        } else if (key == "stagedUtc") {
            marker.stagedUtc = value;
        }
        // Unknown keys are ignored: the marker is additive-only like the
        // manifest, so an older client still reads a newer one.
    }
    marker.valid = sawSchema && marker.schema == 1 && sawVersion && sawSha && sawSize;
    return marker;
}

QByteArray serializeReadyMarker(const ReadyMarker& marker) {
    QString text;
    text += QStringLiteral("schema=1\n");
    text += QStringLiteral("version=%1\n").arg(marker.version);
    text += QStringLiteral("sha256=%1\n").arg(marker.sha256);
    text += QStringLiteral("size=%1\n").arg(marker.size);
    text += QStringLiteral("stagedUtc=%1\n").arg(marker.stagedUtc);
    return text.toUtf8();
}

std::optional<StagedUpdate> readStagedDir(const QString& dirPath) {
    const QFileInfo dirInfo(dirPath);
    const QString name = dirInfo.fileName();
    if (!isValidVersion(name)) { return std::nullopt; }

    QFile markerFile(dirPath + QLatin1Char('/') + QLatin1String(kReadyMarkerName));
    if (!markerFile.open(QIODevice::ReadOnly)) { return std::nullopt; }
    const ReadyMarker marker = parseReadyMarker(markerFile.readAll());
    markerFile.close();
    if (!marker.valid || marker.version != name) { return std::nullopt; }

    const QString exePath = dirPath + QLatin1Char('/') + QLatin1String(kSetupExeName);
    const QFileInfo exeInfo(exePath);
    if (!exeInfo.isFile() || exeInfo.size() != marker.size) { return std::nullopt; }

    StagedUpdate staged;
    staged.version = marker.version;
    staged.dir = dirPath;
    staged.exePath = exePath;
    staged.sha256 = marker.sha256;
    staged.size = marker.size;
    return staged;
}

QString sha256OfFile(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { return {}; }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file)) { return {}; }
    return QString::fromLatin1(hash.result().toHex());
}

FileStagingStore::FileStagingStore(QString root)
    : root_(root.isEmpty() ? updatesRootDir() : std::move(root)) {}

QString FileStagingStore::stagingDir() const { return root_ + QStringLiteral("/staging"); }

QString FileStagingStore::readyDir() const { return root_ + QStringLiteral("/ready"); }

QString FileStagingStore::readyDirFor(const QString& version) const {
    return readyDir() + QLatin1Char('/') + version;
}

QString FileStagingStore::partPathFor(const QString& version) const {
    if (root_.isEmpty()) { return {}; }
    return stagingDir() + QStringLiteral("/dish-setup-") + version + QStringLiteral(".exe.part");
}

std::optional<StagedUpdate> FileStagingStore::findStaged() {
    if (root_.isEmpty()) { return std::nullopt; }
    QDir ready(readyDir());
    if (!ready.exists()) { return std::nullopt; }

    std::optional<StagedUpdate> best;
    const QStringList entries = ready.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        auto staged = readStagedDir(ready.absoluteFilePath(entry));
        if (!staged.has_value()) { continue; }
        if (!best.has_value() || isStrictlyNewer(staged->version, best->version)) {
            best = std::move(staged);
        }
    }
    return best;
}

std::optional<QString> FileStagingStore::promote(const QString& version, const QString& sha256,
                                                 qint64 size, const QByteArray& manifestBytes) {
    if (root_.isEmpty() || !isValidVersion(version)) { return std::nullopt; }

    const QString partPath = partPathFor(version);
    const QFileInfo partInfo(partPath);
    if (!partInfo.isFile() || partInfo.size() != size) { return std::nullopt; }

    // ALWAYS a full re-read from disk. The stream hash proves what was
    // received; this proves what survived to the filesystem, which is what the
    // installer will actually execute.
    if (sha256OfFile(partPath).compare(sha256, Qt::CaseInsensitive) != 0) {
        (void)removeFileRetrying(partPath);
        return std::nullopt;
    }

    const QString target = readyDirFor(version);
    if (!removeTreeRetrying(target)) { return std::nullopt; }
    if (!QDir().mkpath(target)) { return std::nullopt; }

    const QString exePath = target + QLatin1Char('/') + QLatin1String(kSetupExeName);
    const bool moved = retrying([&partPath, &exePath] { return QFile::rename(partPath, exePath); });
    if (!moved) {
        (void)removeTreeRetrying(target);
        return std::nullopt;
    }

    // A byte snapshot of the manifest that described these bytes, so a support
    // request can show what the client was told at stage time.
    if (!manifestBytes.isEmpty()) {
        (void)writeFilePlain(target + QLatin1Char('/') + QLatin1String(kManifestSnapshotName),
                             manifestBytes);
    }

    ReadyMarker marker;
    marker.schema = 1;
    marker.version = version;
    marker.sha256 = sha256.toLower();
    marker.size = size;
    marker.stagedUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    // LAST, and durably: everything above is inert until this lands.
    if (!writeFileDurably(target + QLatin1Char('/') + QLatin1String(kReadyMarkerName),
                          serializeReadyMarker(marker))) {
        (void)removeTreeRetrying(target);
        return std::nullopt;
    }
    return target;
}

void FileStagingStore::discard(const QString& version) {
    if (root_.isEmpty() || version.isEmpty()) { return; }
    (void)removeTreeRetrying(readyDirFor(version));
    (void)removeFileRetrying(partPathFor(version));
}

void FileStagingStore::sweep(const QString& currentVersion) {
    if (root_.isEmpty()) { return; }

    // 1. Partials older than a day. A younger one may belong to a download
    //    running right now, so age is the only safe discriminator.
    QDir staging(stagingDir());
    if (staging.exists()) {
        const QDateTime cutoff =
            QDateTime::currentDateTimeUtc().addMSecs(-reducer::kStagingPartMaxAgeMs);
        const QFileInfoList parts = staging.entryInfoList(QDir::Files);
        for (const QFileInfo& part : parts) {
            if (part.lastModified().toUTC() < cutoff) {
                (void)removeFileRetrying(part.absoluteFilePath());
            }
        }
    }

    // 2. Every ready dir that is junk, or that this build has already caught up
    //    with. The second rule is the post-apply loop breaker AND the answer to
    //    "the installer cannot delete its own directory": the briefly locked
    //    just-used tree simply goes on the next pass.
    QDir ready(readyDir());
    if (!ready.exists()) { return; }
    QString survivor;
    const QStringList entries = ready.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        const QString dirPath = ready.absoluteFilePath(entry);
        const auto staged = readStagedDir(dirPath);
        const bool obsolete =
            !isValidVersion(currentVersion) || !isStrictlyNewer(entry, currentVersion);
        if (!staged.has_value() || obsolete) {
            (void)removeTreeRetrying(dirPath);
            continue;
        }
        if (survivor.isEmpty() || isStrictlyNewer(staged->version, survivor)) {
            if (!survivor.isEmpty()) { (void)removeTreeRetrying(readyDirFor(survivor)); }
            survivor = staged->version;
        } else {
            (void)removeTreeRetrying(dirPath);
        }
    }
}

bool FileStagingStore::hasRoomFor(qint64 assetSize) const {
    if (root_.isEmpty()) { return false; }
    // The volume, not the directory: the tree may not exist yet on a first
    // download, and QStorageInfo resolves the nearest existing ancestor.
    QDir().mkpath(stagingDir());
    const QStorageInfo storage(stagingDir());
    if (!storage.isValid() || !storage.isReady()) { return false; }
    return storage.bytesAvailable() >= assetSize + reducer::kDownloadHeadroomBytes;
}

} // namespace dish::update
