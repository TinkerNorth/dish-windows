// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The --update-apply handoff mode (spec 3.8 + 16.6): the app spawns the
// staged installer and dies; this code waits for that pid, re-locates the
// SPECIFIC install holding --target-exe, gates on --expect-version and
// downgrade, self-elevates exactly once when the recorded scope is machine,
// runs the normal silent upgrade pipeline, writes apply-result.txt in every
// outcome, and honours the relaunch duties (new dish.exe on success, old
// dish.exe with exactly `--no-update-handoff` on failure) unless
// --no-relaunch. Never terminates processes; owns no staging cleanup on
// success (the relaunched app's janitor does).

#pragma once

#include "installer/CliOptions.h"
#include "installer/Logger.h"
#include "installer/ops/FileOps.h"
#include "installer/ops/ProcessOps.h"

class QCoreApplication;

namespace dish::installer {

class UpdateApply {
  public:
    explicit UpdateApply(Logger& logger);

    // Blocks until the apply completes; returns the process exit code
    // (section 9; apply-result.txt carries "<token> <code>" for the app,
    // because the invoking process is dead by then).
    int run(const CliOptions& options, QCoreApplication& app);

  private:
    Logger& logger_;
};

// "<token> <code>" + LF, written atomically (tmp + rename) into `dir`.
// Exposed for tests.
bool writeApplyResult(const QString& dir, int exitCode);

// H6's promise ("under no outcome may the user end with no app") for the ONE
// exit that happens before UpdateApply::run and therefore outside conclude():
// the single-instance gate refusing because a wizard or an uninstaller already
// holds Local\TinkerNorth.DishSetup. By then the app has already returned 0 from
// its boot gate, so returning Busy on its own means the user's launch did
// nothing at all. Restarts --target-exe with exactly the loop breaker, so the
// next start does not hand off again into the same busy installer.
// True when a relaunch was actually issued.
bool relaunchTargetAfterBusy(const CliOptions& options, FileOps& fileOps, ProcessOps& processOps,
                             Logger& logger);

} // namespace dish::installer
