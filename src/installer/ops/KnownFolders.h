// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SHGetKnownFolderPath wrappers plus the derived default locations the
// installer writes to. Everything returns forward-slash Qt paths; the Win32
// ops normalize to native separators at the API boundary. Empty string means
// the shell could not resolve the folder (callers treat that as a failure of
// the operation that needed it, never as "install to CWD").

#pragma once

#include "installer/InstallPlan.h"
#include "installer/ops/ShortcutOps.h"

#include <QString>

namespace dish::installer {

// %LOCALAPPDATA%; the updater cache root %LOCALAPPDATA%/Dish/updates hangs off
// this (built from the environment variable, matching the crash handler's
// convention, not QStandardPaths).
QString localAppDataDir();

// FOLDERID_Programs (per-user) / FOLDERID_CommonPrograms (all-users).
QString programsDir(Scope scope);

// FOLDERID_Desktop (per-user) / FOLDERID_PublicDesktop (all-users).
QString desktopDir(Scope scope);

// The single Dish.lnk each location gets (section 10: no vendor subfolder).
QString shortcutLinkPath(ShortcutLocation location, Scope scope);

// Per-user: FOLDERID_UserProgramFiles\Dish (%LOCALAPPDATA%\Programs\Dish).
// All-users: FOLDERID_ProgramFilesX64\Dish.
QString defaultInstallDir(Scope scope);

// %LOCALAPPDATA%/Dish/updates — removed unconditionally by the uninstaller
// (spec D13).
QString updatesCacheDir();

// %TEMP% resolved wide (never the ANSI short form).
QString tempDir();

} // namespace dish::installer
