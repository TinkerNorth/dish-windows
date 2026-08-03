// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Arms a Win32 unhandled-exception filter that writes a minidump and a
// symbolized crash.log to %LOCALAPPDATA%\Dish\. Nothing is transmitted anywhere
// — the files are the user's to send. Debug builds additionally route MSVC
// debug-CRT assertion reports to the same log and abort, instead of popping the
// modal "Debug Assertion Failed!" dialog.
//
// The filter must never throw or recurse, so it uses only raw Win32 file IO (no
// heap, no Qt, no CRT locale) and a re-entry flag guards a fault inside itself.

#pragma once

namespace dish::crash {

// Idempotent; call early in main(). No-op on non-Windows.
void install();

} // namespace dish::crash
