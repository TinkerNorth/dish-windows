// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/CrashHandler.h"

#include "composer/CrashRearmHook.h"

#if defined(_WIN32)

// <windows.h> must precede dbghelp, which depends on the base Win32 types.
#include <windows.h>

#include <dbghelp.h>
#include <shlobj.h>

#if defined(_DEBUG)
#include <crtdbg.h>
#endif

#include <array>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <typeinfo>

namespace dish::crash {

namespace {

// If the handler itself faults, or a second thread crashes mid-write, bail
// rather than recurse into MiniDumpWriteDump.
LONG g_inHandler = 0;

// Whoever held the top-level filter and the SIGABRT handler before us. The SDK
// installs its own during sentry_init and never calls on, so the files here
// and the report there both see the crash only if we chain to it.
LPTOP_LEVEL_EXCEPTION_FILTER g_prevFilter = nullptr;
using AbortHandler = void (*)(int);
AbortHandler g_prevAbortHandler = nullptr;

// STATUS_FATAL_APP_EXIT, the code the CRT reports an abort under.
constexpr DWORD kStatusFatalAppExit = 0x40000015;

// What std::terminate found, written into the log's header; fixed so the
// terminate path allocates nothing.
char g_terminateReason[256] = {0};

// Resolved once at install() while the heap and CRT are still healthy, so the
// fault path only concatenates fixed buffers — no allocation, no
// SHGetKnownFolderPath from inside the filter.
wchar_t g_crashDir[MAX_PATH] = {0};
wchar_t g_dumpPath[MAX_PATH] = {0};
wchar_t g_logPath[MAX_PATH] = {0};
bool g_pathsReady = false;

// Bounded, allocation-free append.
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
        wappend(g_crashDir, MAX_PATH, L".\\Dish");
    }
    CreateDirectoryW(g_crashDir, nullptr); // ignore "already exists"

    wappend(g_dumpPath, MAX_PATH, g_crashDir);
    wappend(g_dumpPath, MAX_PATH, L"\\crash.dmp");
    wappend(g_logPath, MAX_PATH, g_crashDir);
    wappend(g_logPath, MAX_PATH, L"\\crash.log");
    g_pathsReady = true;
}

// Best-effort, and no CRT / Qt / heap, so it is callable from the filter.
void writeAll(HANDLE file, const char* data, DWORD len) {
    DWORD written = 0;
    while (written < len) {
        DWORD n = 0;
        if (!WriteFile(file, data + written, len - written, &n, nullptr) || n == 0) { return; }
        written += n;
    }
}
void writeStr(HANDLE file, const char* s) { writeAll(file, s, static_cast<DWORD>(std::strlen(s))); }

// "0x" + 16 digits + NUL, hence the 19-byte minimum.
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
// A corrupted frame chain can be cyclic, and this runs inside a crash: the walk stops either way.
constexpr int kMaxStackFrames = 64;

// One frame of the crash log, module then symbol then line, each a dbghelp call with its own
// buffer. Long because it runs inside a crash with no heap of its own, so the buffers stay here
// rather than in helpers that would allocate.
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

void writeHeader(HANDLE log) {
    writeStr(log, "Dish crash report\r\n");
    writeStr(log, "=================\r\n");

    SYSTEMTIME st{};
    GetSystemTime(&st);
    char ts[64];
    std::snprintf(ts, sizeof(ts), "time (UTC): %04u-%02u-%02u %02u:%02u:%02u\r\n", st.wYear,
                  st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    writeStr(log, ts);
    if (g_terminateReason[0] != '\0') {
        writeStr(log, "reason: ");
        writeStr(log, g_terminateReason);
        writeStr(log, "\r\n");
    }
}

// The code always, and for an access violation the operation and address too, which is the
// difference between "it crashed" and "it read from null".
void writeExceptionCode(HANDLE log, const EXCEPTION_RECORD* rec) {
    const DWORD code = (rec != nullptr) ? rec->ExceptionCode : 0;
    char codeLine[64];
    std::snprintf(codeLine, sizeof(codeLine), "exception code: 0x%08lx\r\n",
                  static_cast<unsigned long>(code));
    writeStr(log, codeLine);
    if (code != EXCEPTION_ACCESS_VIOLATION || rec == nullptr || rec->NumberParameters < 2) {
        return;
    }
    // AV info[0] = 0 read / 1 write / 8 DEP; info[1] = the offending address.
    const ULONG_PTR op = rec->ExceptionInformation[0];
    writeStr(log, op == 1 ? "  access violation: WRITE to "
                          : (op == 8 ? "  access violation: EXEC at "
                                     : "  access violation: READ from "));
    char hexbuf[20];
    writeStr(log, hex64(static_cast<std::uint64_t>(rec->ExceptionInformation[1]), hexbuf,
                        sizeof(hexbuf)));
    writeStr(log, "\r\n");
}

void writeFaultingAddress(HANDLE log, HANDLE process, const EXCEPTION_RECORD* rec) {
    const DWORD64 faultAddr =
        (rec != nullptr) ? reinterpret_cast<DWORD64>(rec->ExceptionAddress) : 0;
    char hexbuf[20];
    writeStr(log, "faulting address: ");
    writeStr(log, hex64(static_cast<std::uint64_t>(faultAddr), hexbuf, sizeof(hexbuf)));
    writeStr(log, "\r\n");
    if (faultAddr == 0) { return; }
    writeStr(log, "faulting frame:\r\n");
    writeFrame(log, process, faultAddr);
}

// The machine type and the three registers StackWalk64 needs to start, per architecture.
DWORD seedStackFrame(const CONTEXT& ctx, STACKFRAME64& frame) {
#if defined(_M_X64)
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrStack.Offset = ctx.Rsp;
    const DWORD machine = IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_ARM64)
    frame.AddrPC.Offset = ctx.Pc;
    frame.AddrFrame.Offset = ctx.Fp;
    frame.AddrStack.Offset = ctx.Sp;
    const DWORD machine = IMAGE_FILE_MACHINE_ARM64;
#else
    frame.AddrPC.Offset = ctx.Eip;
    frame.AddrFrame.Offset = ctx.Ebp;
    frame.AddrStack.Offset = ctx.Esp;
    const DWORD machine = IMAGE_FILE_MACHINE_I386;
#endif
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Mode = AddrModeFlat;
    return machine;
}

// Best-effort: a walk that stops early still leaves the frames it reached, which is more than a
// report with no stack at all. StackWalk64 mutates the context, so it gets a copy.
void writeStackWalk(HANDLE log, HANDLE process, HANDLE thread, EXCEPTION_POINTERS* ep) {
    writeStr(log, "stack:\r\n");
    if (ep == nullptr || ep->ContextRecord == nullptr) {
        writeStr(log, "  <no thread context captured>\r\n");
        return;
    }
    CONTEXT ctx = *ep->ContextRecord;
    STACKFRAME64 frame{};
    const DWORD machine = seedStackFrame(ctx, frame);
    // Bounded, because a corrupted frame chain can be cyclic and this runs inside a crash.
    for (int i = 0; i < kMaxStackFrames; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, &ctx, nullptr, SymFunctionTableAccess64,
                         SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) { break; }
        writeFrame(log, process, frame.AddrPC.Offset);
    }
}

void writeCrashLog(EXCEPTION_POINTERS* ep) {
    HANDLE log = CreateFileW(g_logPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) { return; }

    const EXCEPTION_RECORD* rec = (ep != nullptr) ? ep->ExceptionRecord : nullptr;
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    writeHeader(log);
    writeExceptionCode(log, rec);

    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    SymInitialize(process, nullptr, TRUE);
    writeFaultingAddress(log, process, rec);
    writeStackWalk(log, process, thread, ep);

    writeStr(log, "\r\nA matching minidump was written next to this file (crash.dmp).\r\n");
    writeStr(log, "Please send BOTH files to the Dish developers.\r\n");

    SymCleanup(process);
    FlushFileBuffers(log);
    CloseHandle(log);
}

// Passes the exception on to whoever held the filter before us (the SDK's,
// when reporting is armed), or continues the search so WER and a debugger see
// a real crash. Every exit from the filter goes through here, so no early
// return can swallow the crash.
LONG chainOrContinue(EXCEPTION_POINTERS* ep) {
    if (g_prevFilter != nullptr) { return g_prevFilter(ep); }
    return EXCEPTION_CONTINUE_SEARCH;
}

LONG WINAPI unhandledFilter(EXCEPTION_POINTERS* ep) {
    // Single-shot: if we're already inside (re-entrant fault / second crashing
    // thread), hand on rather than recurse.
    if (InterlockedExchange(&g_inHandler, 1) != 0) { return chainOrContinue(ep); }
    if (g_pathsReady) {
        writeMiniDump(ep);
        writeCrashLog(ep);
    }
    return chainOrContinue(ep);
}

// abort() never reaches an exception filter, so it is caught here: the files
// are written from a context captured on the spot under the code the CRT
// reports an abort under, then the previous handler (the SDK's, when armed)
// takes its turn. The CRT terminates the process when this returns.
void onAbortSignal(int signum) {
    if (InterlockedExchange(&g_inHandler, 1) == 0 && g_pathsReady) {
        CONTEXT ctx{};
        RtlCaptureContext(&ctx);
        EXCEPTION_RECORD rec{};
        rec.ExceptionCode = kStatusFatalAppExit;
        rec.ExceptionFlags = EXCEPTION_NONCONTINUABLE;
#if defined(_M_ARM64)
        rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Pc);
#elif defined(_M_X64)
        rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Rip);
#else
        rec.ExceptionAddress = reinterpret_cast<PVOID>(ctx.Eip);
#endif
        EXCEPTION_POINTERS ep{&rec, &ctx};
        writeMiniDump(&ep);
        writeCrashLog(&ep);
    }
    if (g_prevAbortHandler != nullptr && g_prevAbortHandler != SIG_IGN &&
        g_prevAbortHandler != SIG_DFL) {
        g_prevAbortHandler(signum);
    }
}

// An uncaught exception names itself in the log, then aborts, which is the
// path above. No allocation beyond what the CRT's own terminate already did.
void onTerminate() {
    const char* reason = "std::terminate: no active exception";
    if (std::exception_ptr active = std::current_exception()) {
        try {
            std::rethrow_exception(active);
        } catch (const std::exception& e) {
            std::snprintf(g_terminateReason, sizeof(g_terminateReason), "std::terminate: %s: %s",
                          typeid(e).name(), e.what());
            reason = nullptr;
        } catch (...) { reason = "std::terminate: non-standard exception"; }
    }
    if (reason != nullptr) {
        std::snprintf(g_terminateReason, sizeof(g_terminateReason), "%s", reason);
    }
    std::abort();
}

// A pure-virtual call or an invalid CRT parameter would otherwise fast-fail
// past every handler; naming them and aborting routes them through the files.
void onPureCall() {
    std::snprintf(g_terminateReason, sizeof(g_terminateReason), "pure virtual function call");
    std::abort();
}

void onInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int,
                        uintptr_t) {
    std::snprintf(g_terminateReason, sizeof(g_terminateReason), "invalid CRT parameter");
    std::abort();
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
    // WER stays armed and quiet: it is the only recorder left for a fast-fail
    // (a stack cookie, CFG), which never reaches a filter; only its dialog goes.
    SetErrorMode(SEM_FAILCRITICALERRORS);
    rearmFilterChain();
    std::set_terminate(onTerminate);
    _set_purecall_handler(onPureCall);
    _set_invalid_parameter_handler(onInvalidParameter);
    setRearmHook(&rearmFilterChain);
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

void rearmFilterChain() {
    LPTOP_LEVEL_EXCEPTION_FILTER prev = SetUnhandledExceptionFilter(unhandledFilter);
    // Never chain to ourselves, which would recurse until the stack is gone:
    // prev == unhandledFilter means nothing was installed after us.
    if (prev != unhandledFilter) { g_prevFilter = prev; }

    AbortHandler prevAbort = std::signal(SIGABRT, onAbortSignal);
    if (prevAbort != SIG_ERR && prevAbort != onAbortSignal) { g_prevAbortHandler = prevAbort; }
}

} // namespace dish::crash

#else // !_WIN32

namespace dish::crash {
void install() {}
void rearmFilterChain() {}
} // namespace dish::crash

#endif
