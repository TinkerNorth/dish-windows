// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The MSVC debug CRT's default assertion report is a modal dialog; under ctest
// nobody can dismiss it and the test process hangs forever. Route reports to
// stderr and abort instead, so an assertion surfaces as a FAILED test.

#if defined(_WIN32) && defined(_DEBUG)

#include <crtdbg.h>

#include <cstdio>
#include <cstdlib>

namespace {

int assertToStderrHook(int reportType, char* message, int* returnValue) {
    if (returnValue != nullptr) { *returnValue = 0; } // never invoke the debugger
    if (message != nullptr) { std::fputs(message, stderr); }
    if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
        std::fflush(stderr);
        std::abort(); // non-zero exit so ctest records a failed test, not a hang
    }
    return 1; // handled — suppress the default modal report
}

struct InstallAssertHook {
    InstallAssertHook() {
        const int reports[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
        for (const int report : reports) {
            _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
            _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
        }
        _CrtSetReportHook(assertToStderrHook);
        // abort() itself must not pop the "abort has been called" / WER dialog.
        _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    }
};

[[maybe_unused]] const InstallAssertHook g_installAssertHook;

} // namespace

#endif
