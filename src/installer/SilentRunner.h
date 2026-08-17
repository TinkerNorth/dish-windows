// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The headless driver for SilentInstall / SilentUninstall / ExtractOnly
// (spec 3.8): constructs the Win32 ops, applies the silent gates (no UI, no
// UAC ever — machine scope unelevated exits 4), drives the right coordinator
// on the QCoreApplication event loop, and maps `finished` to the section 9
// exit code. --update-apply lives in UpdateApply, the one silent mode allowed
// to raise UAC.

#pragma once

#include "installer/CliOptions.h"
#include "installer/Logger.h"

class QCoreApplication;

namespace dish::installer {

class SilentRunner {
  public:
    explicit SilentRunner(Logger& logger);

    // Blocks (local QEventLoop) until the run completes; returns the process
    // exit code. The log is always written; the exit code + log are the
    // authoritative interfaces (spec 9).
    int run(const CliOptions& options, QCoreApplication& app);

  private:
    int runInstall(const CliOptions& options, QCoreApplication& app);
    int runUninstall(const CliOptions& options, QCoreApplication& app);
    int runExtractOnly(const CliOptions& options);

    Logger& logger_;
};

} // namespace dish::installer
