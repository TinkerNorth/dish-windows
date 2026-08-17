// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The abstract filesystem seam (DisplaySleepInhibitor pattern: an abstract base
// so tests pin behaviour against fakes without touching the real disk). Every
// coordinator effect that mutates the filesystem goes through this interface;
// Win32FileOps is the production implementation.

#pragma once

#include "installer/Errors.h"

#include <QString>
#include <QStringList>

#include <functional>

namespace dish::installer {

// The uniform result of any single operation. `win32` is the raw GetLastError
// value (0 when unknown or not applicable); `error` is the typed classification
// reducers act on; `path` names the object that failed so logs and the UI can
// point at it.
struct OpResult {
    bool ok = true;
    quint32 win32 = 0;
    SetupError error = SetupError::None;
    QString path;

    static OpResult success(const QString& path = QString()) {
        return OpResult{true, 0, SetupError::None, path};
    }
    static OpResult failure(SetupError error, const QString& path, quint32 win32 = 0) {
        return OpResult{false, win32, error, path};
    }
    bool operator==(const OpResult& o) const {
        return ok == o.ok && win32 == o.win32 && error == o.error && path == o.path;
    }
    bool operator!=(const OpResult& o) const { return !(*this == o); }
};

class FileOps {
  public:
    virtual ~FileOps() = default;
    // `onBytes` receives byte deltas as the copy streams; returning false
    // cancels the copy, which reports SetupError::Cancelled.
    virtual OpResult copyWithProgress(const QString& from, const QString& to,
                                      const std::function<bool(qint64)>& onBytes) = 0;
    virtual OpResult verifySha256(const QString& path, const QByteArray& expectedHex) = 0;
    virtual OpResult rename(const QString& from, const QString& to) = 0;
    virtual OpResult remove(const QString& path) = 0;
    virtual OpResult ensureDir(const QString& path) = 0;
    virtual OpResult removeDirIfEmpty(const QString& path) = 0;
    virtual qint64 freeBytesFor(const QString& path) = 0;
    virtual bool exists(const QString& path) = 0;
    // Absolute paths of the FILES under `dir`, recursively; no directories.
    virtual QStringList listRecursive(const QString& dir) = 0;
};

} // namespace dish::installer
