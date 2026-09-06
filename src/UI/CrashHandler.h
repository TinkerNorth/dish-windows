// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Arms a Win32 unhandled-exception filter that writes a minidump and a
// symbolized crash.log to %LOCALAPPDATA%\Dish\. These files are always written
// and are the user's to keep; this handler transmits nothing itself.
//
// Uploading is a separate, opt-out concern that lives behind the Settings
// switch: see composer/CrashReportingBackend.h. This handler is armed first
// in main(), before the preference has even been read, because a crash during
// startup still deserves a local artifact.
//
// Debug builds additionally route MSVC debug-CRT assertion reports to the same
// log and abort, instead of popping the modal "Debug Assertion Failed!" dialog.
//
// The filter must never throw or recurse, so it uses only raw Win32 file IO (no
// heap, no Qt, no CRT locale) and a re-entry flag guards a fault inside itself.

#pragma once

namespace dish::crash {

// Idempotent; call early in main(). No-op on non-Windows.
void install();

} // namespace dish::crash
