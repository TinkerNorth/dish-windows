// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The production FileOps: CopyFileExW with progress, BCrypt SHA-256, wide
// long-path (\\?\) forms throughout, and an AV-absorbing retry wrapper around
// every destructive call (5 tries, 250 ms * 2^n) so a transient scanner lock
// surfaces as a delay, not a rollback.

#pragma once

#include "installer/ops/FileOps.h"

namespace dish::installer {

// "C:/x/y" -> "\\?\C:\x\y", "//server/share/x" -> "\\?\UNC\server\share\x".
// Already-prefixed input passes through unchanged.
QString toExtendedPath(const QString& path);

class Win32FileOps : public FileOps {
  public:
    Win32FileOps() = default;
    ~Win32FileOps() override = default;

    OpResult copyWithProgress(const QString& from, const QString& to,
                              const std::function<bool(qint64)>& onBytes) override;
    OpResult verifySha256(const QString& path, const QByteArray& expectedHex) override;
    OpResult rename(const QString& from, const QString& to) override;
    OpResult remove(const QString& path) override;
    OpResult ensureDir(const QString& path) override;
    OpResult removeDirIfEmpty(const QString& path) override;
    qint64 freeBytesFor(const QString& path) override;
    bool exists(const QString& path) override;
    QStringList listRecursive(const QString& dir) override;
};

// Best-effort recursive removal of `root` (files first, then dirs bottom-up)
// through the ops seam, so fakes observe it too. Returns the first failure but
// keeps going; a leftover is logged by the caller, never fatal by itself.
OpResult removeTreeBestEffort(FileOps& ops, const QString& root);

} // namespace dish::installer
