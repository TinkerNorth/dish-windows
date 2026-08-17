// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The single authority for the installer failure taxonomy. SetupError is what
// went wrong (carried through reducers, coordinators and the QML facade);
// ExitCode is the process contract of spec section 9; resultToken is the
// apply-result.txt vocabulary asserted by CI. Nothing else may renumber or
// respell any of these.

#pragma once

namespace dish::installer {

enum class SetupError {
    None,
    Internal,
    Usage,
    UnsupportedOs,
    NeedElevation,
    AppRunning,
    DiskFull,
    PayloadCorrupt,
    FileOpFailed,
    RegistryFailed,
    ShortcutFailed,
    RollbackIncomplete,
    Cancelled,
    NothingInstalled,
    Downgrade,
    Busy,
    VersionMismatch, // --update-apply --expect-version disagreement
};

enum class ExitCode : int { // section 9 table; toExitCode(SetupError) maps them
    Ok = 0,
    Internal = 1,
    Usage = 2,
    UnsupportedOs = 3,
    Elevation = 4,
    AppRunning = 5,
    DiskFull = 6,
    PayloadCorrupt = 7,
    RolledBack = 8,
    RollbackIncomplete = 9,
    Cancelled = 10,
    NothingInstalled = 11,
    Downgrade = 12,
    Busy = 13,
    VersionMismatch = 14,
};

// FileOpFailed / RegistryFailed / ShortcutFailed all land on RolledBack: by the
// time the process exits the journal has been replayed, and the exit code
// reports the outcome, not the trigger. A rollback that itself failed reports
// through SetupError::RollbackIncomplete, which the reducers substitute for the
// original cause the moment a rollback step misses.
constexpr ExitCode toExitCode(SetupError error) {
    switch (error) {
    case SetupError::None:
        return ExitCode::Ok;
    case SetupError::Internal:
        return ExitCode::Internal;
    case SetupError::Usage:
        return ExitCode::Usage;
    case SetupError::UnsupportedOs:
        return ExitCode::UnsupportedOs;
    case SetupError::NeedElevation:
        return ExitCode::Elevation;
    case SetupError::AppRunning:
        return ExitCode::AppRunning;
    case SetupError::DiskFull:
        return ExitCode::DiskFull;
    case SetupError::PayloadCorrupt:
        return ExitCode::PayloadCorrupt;
    case SetupError::FileOpFailed:
        return ExitCode::RolledBack;
    case SetupError::RegistryFailed:
        return ExitCode::RolledBack;
    case SetupError::ShortcutFailed:
        return ExitCode::RolledBack;
    case SetupError::RollbackIncomplete:
        return ExitCode::RollbackIncomplete;
    case SetupError::Cancelled:
        return ExitCode::Cancelled;
    case SetupError::NothingInstalled:
        return ExitCode::NothingInstalled;
    case SetupError::Downgrade:
        return ExitCode::Downgrade;
    case SetupError::Busy:
        return ExitCode::Busy;
    case SetupError::VersionMismatch:
        return ExitCode::VersionMismatch;
    }
    return ExitCode::Internal;
}

// Apply-result tokens (section 9, right-hand column). Written by --update-apply
// into apply-result.txt as "<token> <code>" and parsed by the app's Settings
// failure copy and by CI assertions. ASCII by design; never localized.
constexpr const char* resultToken(ExitCode code) {
    switch (code) {
    case ExitCode::Ok:
        return "ok";
    case ExitCode::Internal:
        return "internal";
    case ExitCode::Usage:
        return "usage";
    case ExitCode::UnsupportedOs:
        return "unsupported-os";
    case ExitCode::Elevation:
        return "elevation-declined";
    case ExitCode::AppRunning:
        return "app-running";
    case ExitCode::DiskFull:
        return "disk-full";
    case ExitCode::PayloadCorrupt:
        return "integrity-failure";
    case ExitCode::RolledBack:
        return "rolled-back";
    case ExitCode::RollbackIncomplete:
        return "rollback-incomplete";
    case ExitCode::Cancelled:
        return "cancelled";
    case ExitCode::NothingInstalled:
        return "nothing-installed";
    case ExitCode::Downgrade:
        return "downgrade-refused";
    case ExitCode::Busy:
        return "busy";
    case ExitCode::VersionMismatch:
        return "version-mismatch";
    }
    return "internal";
}

} // namespace dish::installer
