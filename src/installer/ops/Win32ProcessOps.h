// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The production ProcessOps: Toolhelp32 scan filtered by canonical image-path
// prefix (spec 11.3), WM_CLOSE to top-level windows, elevation probing and
// the two special launches (runas relaunch, explorer-token de-elevated
// start). Everything here is thread-agnostic; the coordinator calls it from
// the worker.

#pragma once

#include "installer/ops/ProcessOps.h"

#include <optional>

namespace dish::installer {

class Win32ProcessOps : public ProcessOps {
  public:
    Win32ProcessOps() = default;
    ~Win32ProcessOps() override = default;

    QVector<ProcInfo> processesUnder(const QString& dir) override;
    bool requestClose(const QVector<ProcInfo>& procs, int timeoutMs) override;
    bool terminate(const QVector<ProcInfo>& procs) override;
    bool waitForPid(quint32 pid, int timeoutMs) override;
    bool isElevated() override;
    OpResult relaunchElevated(const QString& exe, const QStringList& argv) override;
    OpResult launchDetached(const QString& exe, const QStringList& argv, const QString& cwd,
                            bool deElevate) override;

    // Concrete-only (--update-apply H5): runas + wait for the elevated child
    // and hand back its exit code so the parent can mirror it. nullopt when
    // the spawn failed; `declined` distinguishes a refused UAC prompt from
    // other failures.
    std::optional<int> runElevatedWait(const QString& exe, const QStringList& argv, bool* declined);

    // The mapped-module working set of THIS process that lives under `dir`
    // (own exe included): the files a running uninstaller can never delete
    // itself and defers to the helper (spec 3.6).
    QStringList ownWorkingSetUnder(const QString& dir);
};

// Win32 argv quoting (CommandLineToArgvW-compatible): spaces, quotes and
// trailing backslashes handled. Exposed for tests.
QString buildCommandLine(const QString& exe, const QStringList& argv);

} // namespace dish::installer
