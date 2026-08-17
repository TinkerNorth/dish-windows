// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/ops/Win32FileOps.h"

#include <QDir>
#include <QDirIterator>
#include <QThread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <bcrypt.h>
#include <winternl.h>

#include <algorithm>
#include <vector>

namespace dish::installer {

namespace {

constexpr int kRetryCount = 5;
constexpr int kRetryBaseMs = 250;

std::wstring wide(const QString& path) { return toExtendedPath(path).toStdWString(); }

// Sharing violations, lock violations and access-denied are the classic
// transient AV / indexer signatures worth waiting out; everything else fails
// fast because retrying a genuinely bad path only delays the typed error.
bool isTransient(DWORD error) {
    return error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION ||
           error == ERROR_ACCESS_DENIED;
}

template <class Fn> OpResult withRetries(const QString& path, SetupError typedError, Fn&& attempt) {
    DWORD lastError = 0;
    for (int i = 0; i < kRetryCount; ++i) {
        if (attempt(lastError)) { return OpResult::success(path); }
        if (!isTransient(lastError)) { break; }
        if (i + 1 < kRetryCount) { QThread::msleep(static_cast<unsigned long>(kRetryBaseMs << i)); }
    }
    return OpResult::failure(typedError, path, lastError);
}

void clearReadOnly(const std::wstring& widePath) {
    const DWORD attrs = GetFileAttributesW(widePath.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(widePath.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
    }
}

struct CopyContext {
    const std::function<bool(qint64)>* onBytes = nullptr;
    qint64 lastTotal = 0;
    bool cancelled = false;
};

DWORD CALLBACK copyProgressRoutine(LARGE_INTEGER totalFileSize, LARGE_INTEGER totalTransferred,
                                   LARGE_INTEGER /*streamSize*/,
                                   LARGE_INTEGER /*streamTransferred*/, DWORD /*streamNumber*/,
                                   DWORD /*callbackReason*/, HANDLE /*sourceFile*/,
                                   HANDLE /*destinationFile*/, LPVOID data) {
    Q_UNUSED(totalFileSize);
    auto* ctx = static_cast<CopyContext*>(data);
    const qint64 total = totalTransferred.QuadPart;
    const qint64 delta = total - ctx->lastTotal;
    ctx->lastTotal = total;
    if (ctx->onBytes && *ctx->onBytes && delta > 0) {
        if (!(*ctx->onBytes)(delta)) {
            ctx->cancelled = true;
            return PROGRESS_CANCEL;
        }
    }
    return PROGRESS_CONTINUE;
}

} // namespace

QString toExtendedPath(const QString& path) {
    if (path.startsWith(QLatin1String("\\\\?\\"))) { return path; }
    QString native = QDir::toNativeSeparators(QDir::cleanPath(path));
    if (native.startsWith(QLatin1String("\\\\"))) {
        return QStringLiteral("\\\\?\\UNC\\") + native.mid(2);
    }
    return QStringLiteral("\\\\?\\") + native;
}

OpResult Win32FileOps::copyWithProgress(const QString& from, const QString& to,
                                        const std::function<bool(qint64)>& onBytes) {
    const std::wstring wideFrom = wide(from);
    const std::wstring wideTo = wide(to);
    CopyContext ctx;
    ctx.onBytes = &onBytes;
    DWORD lastError = 0;
    for (int i = 0; i < kRetryCount; ++i) {
        ctx.lastTotal = 0;
        ctx.cancelled = false;
        // The destination may exist (repair, retried copy): drop a read-only
        // bit so COPY_FILE_NO_BUFFERING-free replace succeeds.
        clearReadOnly(wideTo);
        BOOL cancelFlag = FALSE;
        if (CopyFileExW(wideFrom.c_str(), wideTo.c_str(), copyProgressRoutine, &ctx, &cancelFlag,
                        0)) {
            return OpResult::success(to);
        }
        lastError = GetLastError();
        if (ctx.cancelled || lastError == ERROR_REQUEST_ABORTED) {
            return OpResult::failure(SetupError::Cancelled, to, lastError);
        }
        if (!isTransient(lastError)) { break; }
        if (i + 1 < kRetryCount) { QThread::msleep(static_cast<unsigned long>(kRetryBaseMs << i)); }
    }
    const SetupError typed = lastError == ERROR_DISK_FULL || lastError == ERROR_HANDLE_DISK_FULL
                                 ? SetupError::DiskFull
                                 : SetupError::FileOpFailed;
    return OpResult::failure(typed, to, lastError);
}

OpResult Win32FileOps::verifySha256(const QString& path, const QByteArray& expectedHex) {
    const std::wstring widePath = wide(path);
    HANDLE file = CreateFileW(widePath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                              OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return OpResult::failure(SetupError::FileOpFailed, path, GetLastError());
    }

    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    OpResult result = OpResult::success(path);
    do {
        if (!BCRYPT_SUCCESS(
                BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            result = OpResult::failure(SetupError::Internal, path);
            break;
        }
        if (!BCRYPT_SUCCESS(BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0))) {
            result = OpResult::failure(SetupError::Internal, path);
            break;
        }
        std::vector<unsigned char> buffer(1024 * 1024);
        for (;;) {
            DWORD read = 0;
            if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) {
                result = OpResult::failure(SetupError::FileOpFailed, path, GetLastError());
                break;
            }
            if (read == 0) { break; }
            if (!BCRYPT_SUCCESS(BCryptHashData(hash, buffer.data(), read, 0))) {
                result = OpResult::failure(SetupError::Internal, path);
                break;
            }
        }
        if (!result.ok) { break; }
        unsigned char digest[32] = {};
        if (!BCRYPT_SUCCESS(BCryptFinishHash(hash, digest, sizeof(digest), 0))) {
            result = OpResult::failure(SetupError::Internal, path);
            break;
        }
        const QByteArray actualHex =
            QByteArray(reinterpret_cast<const char*>(digest), sizeof(digest)).toHex();
        if (actualHex != expectedHex.toLower()) {
            result = OpResult::failure(SetupError::PayloadCorrupt, path);
        }
    } while (false);

    if (hash) { BCryptDestroyHash(hash); }
    if (algorithm) { BCryptCloseAlgorithmProvider(algorithm, 0); }
    CloseHandle(file);
    return result;
}

OpResult Win32FileOps::rename(const QString& from, const QString& to) {
    const std::wstring wideFrom = wide(from);
    const std::wstring wideTo = wide(to);
    return withRetries(to, SetupError::FileOpFailed, [&](DWORD& lastError) {
        if (MoveFileExW(wideFrom.c_str(), wideTo.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return true;
        }
        lastError = GetLastError();
        return false;
    });
}

OpResult Win32FileOps::remove(const QString& path) {
    const std::wstring widePath = wide(path);
    return withRetries(path, SetupError::FileOpFailed, [&](DWORD& lastError) {
        clearReadOnly(widePath);
        if (DeleteFileW(widePath.c_str())) { return true; }
        lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND) {
            return true; // idempotent: already gone is the desired state
        }
        return false;
    });
}

OpResult Win32FileOps::ensureDir(const QString& path) {
    // CreateDirectoryW is not recursive; walk down from the first missing
    // ancestor. QDir::mkpath would work but reports nothing typed.
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    QStringList pending;
    QString probe = clean;
    while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
        pending.prepend(probe);
        const int slash = probe.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0) { break; }
        probe = probe.left(slash);
    }
    for (const QString& dir : pending) {
        const std::wstring wideDir = wide(dir);
        if (!CreateDirectoryW(wideDir.c_str(), nullptr)) {
            const DWORD lastError = GetLastError();
            if (lastError != ERROR_ALREADY_EXISTS) {
                return OpResult::failure(SetupError::FileOpFailed, dir, lastError);
            }
        }
    }
    if (!QFileInfo(clean).isDir()) { return OpResult::failure(SetupError::FileOpFailed, clean); }
    return OpResult::success(clean);
}

OpResult Win32FileOps::removeDirIfEmpty(const QString& path) {
    const std::wstring widePath = wide(path);
    return withRetries(path, SetupError::FileOpFailed, [&](DWORD& lastError) {
        if (RemoveDirectoryW(widePath.c_str())) { return true; }
        lastError = GetLastError();
        if (lastError == ERROR_FILE_NOT_FOUND || lastError == ERROR_PATH_NOT_FOUND) { return true; }
        // Not empty is a normal, expected answer, not a transient lock.
        return false;
    });
}

qint64 Win32FileOps::freeBytesFor(const QString& path) {
    // The queried dir may not exist yet; walk up to the deepest existing
    // ancestor so the answer describes the volume the install will land on.
    QString probe = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (!probe.isEmpty() && !QFileInfo::exists(probe)) {
        const int slash = probe.lastIndexOf(QLatin1Char('/'));
        if (slash < 0) { break; }
        QString parent = probe.left(slash);
        if (parent.size() == 2 && parent[1] == QLatin1Char(':')) { parent += QLatin1Char('/'); }
        // A drive root that does not exist is its own parent ("Z:/" -> "Z:" ->
        // "Z:/"), so the walk has to stop there rather than spin forever on a
        // path naming a volume this machine does not have.
        if (parent == probe) { break; }
        probe = parent;
    }
    if (probe.isEmpty()) { return -1; }
    ULARGE_INTEGER available{};
    const std::wstring wideProbe = wide(probe);
    if (!GetDiskFreeSpaceExW(wideProbe.c_str(), &available, nullptr, nullptr)) { return -1; }
    return static_cast<qint64>(available.QuadPart);
}

bool Win32FileOps::exists(const QString& path) {
    const std::wstring widePath = wide(path);
    return GetFileAttributesW(widePath.c_str()) != INVALID_FILE_ATTRIBUTES;
}

QStringList Win32FileOps::listRecursive(const QString& dir) {
    QStringList files;
    QDirIterator it(dir, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) { files.append(QDir::cleanPath(it.next())); }
    files.sort();
    return files;
}

OpResult removeTreeBestEffort(FileOps& ops, const QString& root) {
    if (!ops.exists(root)) { return OpResult::success(root); }
    OpResult firstFailure = OpResult::success(root);
    const QStringList files = ops.listRecursive(root);
    for (const QString& file : files) {
        const OpResult r = ops.remove(file);
        if (!r.ok && firstFailure.ok) { firstFailure = r; }
    }
    // Directories bottom-up: derive from the real tree (fakes yield nothing
    // here, which is fine — their dirs are implicit).
    QStringList dirs;
    QDirIterator it(root, QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) { dirs.append(QDir::cleanPath(it.next())); }
    std::sort(dirs.begin(), dirs.end(),
              [](const QString& a, const QString& b) { return a.size() > b.size(); });
    for (const QString& dir : dirs) {
        const OpResult r = ops.removeDirIfEmpty(dir);
        if (!r.ok && firstFailure.ok) { firstFailure = r; }
    }
    const OpResult r = ops.removeDirIfEmpty(root);
    if (!r.ok && firstFailure.ok) { firstFailure = r; }
    return firstFailure;
}

} // namespace dish::installer
