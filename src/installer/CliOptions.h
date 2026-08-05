// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The one place the CLI grammar of spec section 9 is parsed. The stub passes
// its verbatim tail to dish-setup-ui.exe; SetupMain parses HERE before any
// Q*Application exists and every mode downstream consumes the typed result.
// toArgv() is the canonical serializer used for the elevation relaunch, so
// parse(toArgv(x)) == x for every CLI-expressible field (probe-derived plan
// fields — isUpgrade/existingVersion/existingDir — are not CLI surface and
// reset to defaults on the round trip by design).

#pragma once

#include "installer/InstallPlan.h"

#include <QString>
#include <QStringList>

#include <variant>

namespace dish::installer {

struct CliOptions {
    enum class Mode {
        UiInstall,
        SilentInstall,
        UiUninstall,
        SilentUninstall,
        UpdateApply,
        ExtractOnly,
        Version,
        Help,
    };
    Mode mode = Mode::UiInstall;
    InstallPlan plan;
    bool purgeUserData = false;
    QString langOverride, logPath, extractDir;
    // update-apply
    quint32 waitPid = 0;
    QString targetExe;
    QString expectVersion;
    bool relaunch = true;
    // internal plumbing
    QString stagingDir, sourceExe;
    bool resumeInstall = false;
    bool elevated = false;

    // `argv` EXCLUDES argv0; `exeBaseName` is argv0's lowercased basename
    // without extension ("uninstall" flips the default mode). Flags are
    // case-insensitive; both `/S` and `--silent`, and NSIS's `/D=<dir>` (which
    // must be last and swallows the unquoted remainder verbatim, spaces
    // included) are accepted. The QString alternative is the usage-error text
    // (ASCII, for the log and --help echo), which callers map to exit 2.
    static std::variant<CliOptions, QString /*usage error*/> parse(const QStringList& argv,
                                                                   const QString& exeBaseName);

    // Canonical form: every mode and choice spelled with its long flag (so the
    // result is basename-independent), booleans always explicit. Exact inverse
    // of parse() per the note above; round-trip pinned by tests.
    QStringList toArgv() const;

    bool operator==(const CliOptions& o) const {
        return mode == o.mode && plan == o.plan && purgeUserData == o.purgeUserData &&
               langOverride == o.langOverride && logPath == o.logPath &&
               extractDir == o.extractDir && waitPid == o.waitPid && targetExe == o.targetExe &&
               expectVersion == o.expectVersion && relaunch == o.relaunch &&
               stagingDir == o.stagingDir && sourceExe == o.sourceExe &&
               resumeInstall == o.resumeInstall && elevated == o.elevated;
    }
    bool operator!=(const CliOptions& o) const { return !(*this == o); }

    bool isSilent() const {
        return mode == Mode::SilentInstall || mode == Mode::SilentUninstall ||
               mode == Mode::UpdateApply || mode == Mode::ExtractOnly;
    }
    bool isUninstall() const { return mode == Mode::UiUninstall || mode == Mode::SilentUninstall; }
};

// The ASCII --help text (spec section 9 grammar). Data, not UI: printed via
// AttachConsole only.
QString cliUsageText();

} // namespace dish::installer
