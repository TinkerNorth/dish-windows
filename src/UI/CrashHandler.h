// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Arms a Win32 unhandled-exception filter that writes a minidump and a
// symbolized crash.log to %LOCALAPPDATA%\Dish\. These files are always written
// and are the user's to keep; this handler transmits nothing itself. It also
// catches abort() (and so std::terminate, a pure-virtual call and an invalid
// CRT parameter, which the CRT turns into one) and writes the same two files.
//
// Uploading is a separate, opt-out concern that lives behind the Settings
// switch: see composer/CrashReportingBackend.h. This handler is armed first
// in main(), before the preference has even been read, because a crash during
// startup still deserves a local artifact. sentry_init later installs
// crashpad's filter and SIGABRT handler on top and never calls on, so the
// backend has this handler re-arm after every arm: back on top, chaining to
// what the SDK installed, so a crash is recorded in both places. Every exit
// from the filter chains or continues the search, so WER still sees the crash
// when nothing else is installed.
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

// Put the filter and the abort handler back on top, remembering what was there
// to chain to. Registered with composer/CrashRearmHook.h by install().
void rearmFilterChain();

} // namespace dish::crash
