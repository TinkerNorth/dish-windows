// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/CrashHandler.h"

#if defined(_WIN32)

// <windows.h> first (NOMINMAX / WIN32_LEAN_AND_MEAN come from the build defs),
// then dbghelp, which depends on the base Win32 types. <shlobj.h> for the
// Known-Folder lookup of %LOCALAPPDATA%.
#include <windows.h>

#include <dbghelp.h>
#include <shlobj.h>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace dish::crash {

namespace {

// Re-entry guard: if the handler itself faults (or a second thread crashes
// while we're mid-write), bail rather than recurse into MiniDumpWriteDump.
LONG g_inHandler = 0;

// The resolved crash directory (%LOCALAPPDATA%\Dish), filled once at install()
// on the main thread while the heap/CRT are healthy, so the fault path only
// concatenates fixed wide-char buffers — no allocation, no SHGetKnownFolderPath
// from inside the filter.
wchar_t g_crashDir[MAX_PATH] = {0};
wchar_t g_dumpPath[MAX_PATH] = {0};
wchar_t g_logPath[MAX_PATH] = {0};
bool g_pathsReady = false;

// Append a NUL-terminated wide string to dst (bounded). Tiny, allocation-free.
void wappend(wchar_t* dst, size_t cap, const wchar_t* src) {
    const size_t cur = wcslen(dst);
    for (size_t i = 0; cur + i + 1 < cap && src[i] != L'\0'; ++i) {
        dst[cur + i] = src[i];
        dst[cur + i + 1] = L'\0';
    }
}

void buildPaths() {
    wchar_t* base = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &base)) &&
        base != nullptr) {
        wappend(g_crashDir, MAX_PATH, base);
        wappend(g_crashDir, MAX_PATH, L"\\Dish");
        CoTaskMemFree(base);
    } else {
        // Fallback to the current directory if the Known Folder lookup fails.
        wappend(g_crashDir, MAX_PATH, L".\\Dish");
    }
    CreateDirectoryW(g_crashDir, nullptr); // ignore "already exists"

    wappend(g_dumpPath, MAX_PATH, g_crashDir);
    wappend(g_dumpPath, MAX_PATH, L"\\crash.dmp");
    wappend(g_logPath, MAX_PATH, g_crashDir);
    wappend(g_logPath, MAX_PATH, L"\\crash.log");
    g_pathsReady = true;
}

// Best-effort raw file write — opens, appends, closes. No CRT, no Qt, no heap.
void writeAll(HANDLE file, const char* data, DWORD len) {
    DWORD written = 0;
    while (written < len) {
        DWORD n = 0;
        if (!WriteFile(file, data + written, len - written, &n, nullptr) || n == 0) { return; }
        written += n;
    }
}
void writeStr(HANDLE file, const char* s) { writeAll(file, s, static_cast<DWORD>(std::strlen(s))); }

// Format an unsigned 64-bit value as hex into buf ("0x...."). Returns buf.
const char* hex64(std::uint64_t v, char* buf, size_t cap) {
    static const char digits[] = "0123456789abcdef";
    if (cap < 19) {
        if (cap > 0) { buf[0] = '\0'; }
        return buf;
    }
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 0; i < 16; ++i) { buf[2 + i] = digits[(v >> ((15 - i) * 4)) & 0xF]; }
    buf[18] = '\0';
    return buf;
}

void writeMiniDump(EXCEPTION_POINTERS* ep) {
    HANDLE file = CreateFileW(g_dumpPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { return; }
    MINIDUMP_EXCEPTION_INFORMATION mei{};
    mei.ThreadId = GetCurrentThreadId();
    mei.ExceptionPointers = ep;
    mei.ClientPointers = FALSE;
    // A "with data segs + handle data" dump is small but carries enough to
    // resolve the faulting frame and locals against the PDB.
    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData |
                                                 MiniDumpWithThreadInfo);
    MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                      ep != nullptr ? &mei : nullptr, nullptr, nullptr);
    CloseHandle(file);
}

// Symbolize one address into the log: "  0x.... module!symbol+0xNN" or the raw
// address when symbols are unavailable. Bounded scratch; no heap of our own
// (dbghelp may allocate internally, but we are already past the point of no
// return and best-effort).
void writeFrame(HANDLE log, HANDLE process, DWORD64 addr) {
    char hexbuf[20];
    writeStr(log, "  ");
    writeStr(log, hex64(addr, hexbuf, sizeof(hexbuf)));
    writeStr(log, "  ");

    // Module name.
    IMAGEHLP_MODULE64 mod{};
    mod.SizeOfStruct = sizeof(mod);
    if (SymGetModuleInfo64(process, addr, &mod)) {
        writeStr(log, mod.ModuleName);
    } else {
        writeStr(log, "<module?>");
    }

    // Symbol + displacement.
    alignas(SYMBOL_INFO) std::array<std::uint8_t, sizeof(SYMBOL_INFO) + 512> symBuf{};
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf.data());
    sym->SizeOfStruct = sizeof(SYMBOL_INFO);
    sym->MaxNameLen = 511;
    DWORD64 disp = 0;
    if (SymFromAddr(process, addr, &disp, sym)) {
        writeStr(log, "!");
        writeStr(log, sym->Name);
        writeStr(log, "+");
        writeStr(log, hex64(disp, hexbuf, sizeof(hexbuf)));
    }

    // Source file:line, when the PDB carries line info.
    IMAGEHLP_LINE64 lineInfo{};
    lineInfo.SizeOfStruct = sizeof(lineInfo);
    DWORD lineDisp = 0;
    if (SymGetLineFromAddr64(process, addr, &lineDisp, &lineInfo)) {
        writeStr(log, " (");
        writeStr(log, lineInfo.FileName);
        writeStr(log, ":");
        char num[16];
        std::snprintf(num, sizeof(num), "%lu", static_cast<unsigned long>(lineInfo.LineNumber));
        writeStr(log, num);
        writeStr(log, ")");
    }
    writeStr(log, "\r\n");
}

void writeCrashLog(EXCEPTION_POINTERS* ep) {
    HANDLE log = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) { return; }

    char hexbuf[20];
    writeStr(log, "Dish crash report\r\n");
    writeStr(log, "=================\r\n");

    // Timestamp (UTC).
    SYSTEMTIME st{};
    GetSystemTime(&st);
    char ts[64];
    std::snprintf(ts, sizeof(ts), "time (UTC): %04u-%02u-%02u %02u:%02u:%02u\r\n", st.wYear,
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    writeStr(log, ts);

    const EXCEPTION_RECORD* rec = (ep != nullptr) ? ep->ExceptionRecord : nullptr;
    const DWORD code = (rec != nullptr) ? rec->ExceptionCode : 0;
    char codeLine[64];
    std::snprintf(codeLine, sizeof(codeLine), "exception code: 0x%08lx\r\n",
                  static_cast<unsigned long>(code));
    writeStr(log, codeLine);
    if (code == EXCEPTION_ACCESS_VIOLATION && rec != nullptr && rec->NumberParameters >= 2) {
        // AV info[0] = 0 read / 1 write / 8 DEP; info[1] = the offending address.
        const ULONG_PTR op = rec->ExceptionInformation[0];
        writeStr(log, op == 1 ? "  access violation: WRITE to "
                              : (op == 8 ? "  access violation: EXEC at "
                                         : "  access violation: READ from "));
        writeStr(log, hex64(static_cast<std::uint64_t>(rec->ExceptionInformation[1]), hexbuf,
                            sizeof(hexbuf)));
        writeStr(log, "\r\n");
    }

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    // Faulting instruction address + its module.
    const DWORD64 faultAddr =
        (rec != nullptr) ? reinterpret_cast<DWORD64>(rec->ExceptionAddress) : 0;
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);

    writeStr(log, "faulting address: ");
    writeStr(log, hex64(static_cast<std::uint64_t>(faultAddr), hexbuf, sizeof(hexbuf)));
    writeStr(log, "\r\n");
    if (faultAddr != 0) {
        writeStr(log, "faulting frame:\r\n");
        writeFrame(log, process, faultAddr);
    }

    // Best-effort stack walk from the captured context.
    writeStr(log, "stack:\r\n");
    if (ep != nullptr && ep->ContextRecord != nullptr) {
        CONTEXT ctx = *ep->ContextRecord;
        STACKFRAME64 frame{};
        DWORD machine;
#if defined(_M_X64)
        machine = IMAGE_FILE_MACHINE_AMD64;
        frame.AddrPC.Offset = ctx.Rip;
        frame.AddrFrame.Offset = ctx.Rbp;
        frame.AddrStack.Offset = ctx.Rsp;
#elif defined(_M_ARM64)
        machine = IMAGE_FILE_MACHINE_ARM64;
        frame.AddrPC.Offset = ctx.Pc;
        frame.AddrFrame.Offset = ctx.Fp;
        frame.AddrStack.Offset = ctx.Sp;
#else
        machine = IMAGE_FILE_MACHINE_I386;
        frame.AddrPC.Offset = ctx.Eip;
        frame.AddrFrame.Offset = ctx.Ebp;
        frame.AddrStack.Offset = ctx.Esp;
#endif
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 64; ++i) {
            if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
                break;
            }
            if (frame.AddrPC.Offset == 0) { break; }
            writeFrame(log, process, frame.AddrPC.Offset);
        }
    } else {
        writeStr(log, "  <no thread context captured>\r\n");
    }

    writeStr(log, "\r\nA matching minidump was written next to this file (crash.dmp).\r\n");
    writeStr(log, "Please send BOTH files to the Dish developers.\r\n");

    SymCleanup(process);
    FlushFileBuffers(log);
    CloseHandle(log);
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep) {
    // Single-shot: if we're already inside (re-entrant fault / second crashing
    // thread), let the OS take it rather than recurse.
    if (InterlockedExchange(&g_inHandler, 1) != 0) { return EXCEPTION_EXECUTE_HANDLER; }
    if (g_pathsReady) {
        writeMiniDump(ep);
        writeCrashLog(ep);
    }
    // Hand back to the OS so WER / the debugger still see a real crash and the
    // process actually terminates.
    return EXCEPTION_EXECUTE_HANDLER;
}

#if defined(_DEBUG)
// Route a debug-CRT assert / error (e.g. _STL_VERIFY on an empty std::optional
// deref) into crash.log + abort, instead of the modal "Debug Assertion Failed!"
// dialog. Mirrors tests/CrtAssertToStderr.cpp, but persists to the same file the
// SEH path uses so a user running build-debug\dish.exe leaves us the message +
// location.
int assertToCrashLog(int reportType, char* message, int* returnValue) {
    if (returnValue != nullptr) { *returnValue = 0; } // never invoke the debugger
    if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
        if (g_pathsReady && InterlockedExchange(&g_inHandler, 1) == 0) {
            HANDLE log = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (log != INVALID_HANDLE_VALUE) {
                writeStr(log, "Dish debug-CRT assertion failure\r\n");
                writeStr(log, "================================\r\n");
                if (message != nullptr) { writeStr(log, message); }
                writeStr(log, "\r\n");
                FlushFileBuffers(log);
                CloseHandle(log);
            }
        }
        std::fflush(stderr);
        std::abort(); // non-zero exit; no modal dialog
    }
    return 1; // handled — suppress the default modal report for WARN too
}
#endif

bool g_installed = false;

} // namespace

void install() {
    if (g_installed) { return; }
    g_installed = true;
    buildPaths();
    SetUnhandledExceptionFilter(unhandledFilter);
#if defined(_DEBUG)
    const int reports[] = {_CRT_WARN, _CRT_ERROR, _CRT_ASSERT};
    for (const int report : reports) {
        _CrtSetReportMode(report, _CRTDBG_MODE_FILE);
        _CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
    }
    _CrtSetReportHook(assertToCrashLog);
    // abort() must not pop the "abort has been called" / WER dialog on top.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
}

} // namespace dish::crash

#else // !_WIN32

namespace dish::crash {
void install() {}
} // namespace dish::crash

#endif
