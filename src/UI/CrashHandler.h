// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// CrashHandler — a tiny, dependency-light crash diagnostic for the Dish app.
//
// On install() it arms a Win32 SetUnhandledExceptionFilter that, on any
// unhandled structured exception (access violation, etc.), writes:
//   * %LOCALAPPDATA%\Dish\crash.dmp  — a minidump (MiniDumpWriteDump), and
//   * %LOCALAPPDATA%\Dish\crash.log  — a one-shot text record: the faulting
//     code address + owning module, the exception code, and a best-effort
//     symbolized return-address stack (StackWalk64 / SymFromAddr, falling back
//     to raw addresses when symbols aren't present).
//
// It ALSO routes the MSVC debug-CRT assertion/error reports (_CRT_ASSERT /
// _CRT_ERROR — e.g. an empty std::optional deref tripping _STL_VERIFY in a
// debug build) to the SAME crash.log and aborts, instead of popping the modal
// "Debug Assertion Failed!" dialog. This mirrors tests/CrtAssertToStderr.cpp,
// but for the shipping app's build-debug\dish.exe a user might run.
//
// Everything is guarded so the handler itself can never throw or recurse: the
// filter uses only async-signal-safe-ish Win32 file IO (no heap, no Qt, no
// CRT locale), writes are best-effort, and a re-entry flag suppresses a fault
// inside the handler. Release builds ship NDEBUG so the CRT-assert hook is a
// no-op there; the SEH filter is always active.

#pragma once

namespace dish::crash {

// Install the unhandled-exception filter + the debug-CRT assert redirect.
// Idempotent and safe to call once early in main(). No-op on non-Windows.
void install();

} // namespace dish::crash
