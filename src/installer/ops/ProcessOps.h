// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The abstract process seam: the running-app gate, elevation, and detached
// launches. Win32ProcessOps is the production implementation; tests drive the
// reducers and coordinators through fakes.

#pragma once

#include "installer/ops/FileOps.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace dish::installer {

struct ProcInfo {
    quint32 pid = 0;
    QString imagePath;
    QString name;
    bool operator==(const ProcInfo& o) const {
        return pid == o.pid && imagePath == o.imagePath && name == o.name;
    }
    bool operator!=(const ProcInfo& o) const { return !(*this == o); }
};

class ProcessOps {
  public:
    virtual ~ProcessOps() = default;
    // Processes whose canonical image path lives under `dir` (case-folded,
    // \\?\-normalized prefix compare), so a portable dish.exe elsewhere never
    // blocks and renamed or duplicated exes are caught.
    virtual QVector<ProcInfo> processesUnder(const QString& dir) = 0;
    // WM_CLOSE to each pid's top-level windows, then poll until every listed
    // process is gone or the timeout lapses. True when all exited.
    virtual bool requestClose(const QVector<ProcInfo>& procs, int timeoutMs) = 0;
    virtual bool terminate(const QVector<ProcInfo>& procs) = 0;
    // True once the pid has exited (a pid that no longer exists counts as
    // exited); false on timeout.
    virtual bool waitForPid(quint32 pid, int timeoutMs) = 0;
    virtual bool isElevated() = 0;
    // ShellExecuteExW "runas". A declined UAC prompt reports
    // SetupError::NeedElevation; the caller decides what a decline means.
    virtual OpResult relaunchElevated(const QString& exe, const QStringList& argv) = 0;
    // Fire-and-forget launch. `deElevate` asks for the unelevated shell token
    // when the current process is elevated (explorer token); when the process
    // is not elevated it is a plain launch. If de-elevation is requested but
    // impossible, the launch is skipped and reported as a failure rather than
    // running the target elevated.
    virtual OpResult launchDetached(const QString& exe, const QStringList& argv, const QString& cwd,
                                    bool deElevate) = 0;
};

} // namespace dish::installer
