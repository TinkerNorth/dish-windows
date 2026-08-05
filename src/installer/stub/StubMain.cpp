// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// dish-setup.exe stub: the only Dish binary that must run on a machine with
// NOTHING installed. Static CRT, no Qt, Win32 only (kernel32 user32 shell32
// comctl32 advapi32). Flow: OS gate -> trailer + CRC gate -> %TEMP% preflight
// -> extract the payload (marquee unless silent) -> spawn the extracted
// dish-setup-ui.exe with the verbatim original argument tail -> wait -> clean
// staging -> propagate the child's exit code.

#include "StubUi.h"

#include "../PayloadFormat.h"
#include "StubStrings.h"

// windows.h arrives via StubStrings.h.
#include <shellapi.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <string>

// RtlGenRandom: advapi32's link-time name for the system RNG (used for the
// staging directory suffix; no CryptoAPI/bcrypt dependency).
extern "C" BOOLEAN APIENTRY SystemFunction036(PVOID buffer, ULONG length);

namespace {

namespace payload = dish::installer::payload;
namespace stub = dish::installer::stub;

// Exit codes from the spec's single 0-14 table. src/installer/Errors.h is the
// authority; the stub hardcodes the integers so it never includes Qt-side
// headers.
constexpr int kExitInternal = 1;
constexpr int kExitUnsupportedOs = 3;
constexpr int kExitDiskFull = 6;
constexpr int kExitPayloadCorrupt = 7;
constexpr int kExitCancelled = 10;

constexpr uint64_t kTempFreeSpaceMargin = 64ull * 1024 * 1024; // beyond the unpacked image

constexpr wchar_t kHelpText[] =
    L"Dish Setup\r\n"
    L"Usage: dish-setup.exe [options]\r\n"
    L"  /S | --silent                silent install (no UI, no UAC)\r\n"
    L"  /D=<dir>                     install dir (NSIS compat; must be last, unquoted)\r\n"
    L"  --dir <dir>                  install dir\r\n"
    L"  --scope user|machine         install scope (default user)\r\n"
    L"  --start-menu on|off          Start Menu shortcut (default on)\r\n"
    L"  --desktop on|off             desktop shortcut (default off)\r\n"
    L"  --launch on|off              launch Dish when done\r\n"
    L"  --closeapps                  close a running Dish gracefully\r\n"
    L"  --forceclose                 close a running Dish, forcefully if needed\r\n"
    L"  --allow-downgrade            permit installing an older version\r\n"
    L"  --lang <code>                UI language (system|en|bs|de|es|fr|pt_BR)\r\n"
    L"  --log <file>                 log file path\r\n"
    L"  --extract-only <dir>         unpack the install image only, then exit\r\n"
    L"  --update-apply ...           auto-update handoff mode (see docs/INSTALLER.md)\r\n"
    L"  --version                    print the version and exit\r\n"
    L"  --help                       print this help and exit\r\n"
    L"Exit codes and the full grammar: docs/INSTALLER.md.\r\n";

// Stub-side diagnostics: OutputDebugString plus a best-effort append to
// %TEMP%\dish-setup-stub.log (the full Logger lives in dish-setup-ui.exe; the
// stub only records the ARM64 note and cleanup stragglers). Never fails the
// flow.
void debugLog(const wchar_t* message) {
    OutputDebugStringW(L"dish-setup: ");
    OutputDebugStringW(message);
    OutputDebugStringW(L"\n");

    wchar_t temp[MAX_PATH + 1];
    const DWORD n = GetTempPathW(MAX_PATH + 1, temp);
    if (n == 0 || n > MAX_PATH) return;
    const std::wstring path = std::wstring(temp, n) + L"dish-setup-stub.log";
    const HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                    OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    wchar_t prefix[32];
    swprintf(prefix, 32, L"[pid %lu] ", static_cast<unsigned long>(GetCurrentProcessId()));
    const std::wstring line = prefix + std::wstring(message);
    const int chars = static_cast<int>(line.size());
    const int bytes =
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), chars, nullptr, 0, nullptr, nullptr);
    if (bytes > 0) {
        std::string utf8(static_cast<size_t>(bytes) + 2, '\0');
        WideCharToMultiByte(CP_UTF8, 0, line.c_str(), chars, utf8.data(), bytes, nullptr, nullptr);
        utf8[static_cast<size_t>(bytes)] = '\r';
        utf8[static_cast<size_t>(bytes) + 1] = '\n';
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }
    CloseHandle(file);
}

// Best-effort console echo for --help/--version: a GUI-subsystem binary has no
// console of its own, so attach to the parent's (the exit code and log stay
// the authoritative interfaces).
void writeToConsole(const wchar_t* text) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE opened = nullptr;
    if (out == nullptr || out == INVALID_HANDLE_VALUE) {
        AttachConsole(ATTACH_PARENT_PROCESS);
        out = GetStdHandle(STD_OUTPUT_HANDLE);
        if (out == nullptr || out == INVALID_HANDLE_VALUE) {
            opened = CreateFileW(L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_READ,
                                 nullptr, OPEN_EXISTING, 0, nullptr);
            out = opened;
        }
    }
    if (out == nullptr || out == INVALID_HANDLE_VALUE) return;
    DWORD mode = 0;
    const int textLen = static_cast<int>(std::wcslen(text));
    if (GetConsoleMode(out, &mode)) {
        DWORD written = 0;
        WriteConsoleW(out, text, static_cast<DWORD>(textLen), &written, nullptr);
    } else {
        // Redirected handle (file/pipe): write UTF-8 bytes.
        const int bytes =
            WideCharToMultiByte(CP_UTF8, 0, text, textLen, nullptr, 0, nullptr, nullptr);
        if (bytes > 0) {
            std::string utf8(static_cast<size_t>(bytes), '\0');
            WideCharToMultiByte(CP_UTF8, 0, text, textLen, utf8.data(), bytes, nullptr, nullptr);
            DWORD written = 0;
            WriteFile(out, utf8.data(), static_cast<DWORD>(bytes), &written, nullptr);
        }
    }
    if (opened) CloseHandle(opened);
}

// The raw GetCommandLineW() with the program token removed: passed to
// dish-setup-ui.exe VERBATIM (never re-tokenized or re-quoted, which is what
// keeps NSIS-style "/D=C:\Some Dir" intact).
std::wstring commandLineTail() {
    const wchar_t* p = GetCommandLineW();
    if (*p == L'"') {
        ++p;
        while (*p != L'\0' && *p != L'"') ++p;
        if (*p == L'"') ++p;
    } else {
        while (*p != L'\0' && *p != L' ' && *p != L'\t') ++p;
    }
    while (*p == L' ' || *p == L'\t') ++p;
    return p;
}

std::wstring moduleFileName() {
    std::wstring buf(1024, L'\0');
    for (;;) {
        const DWORD n = GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (n == 0) return std::wstring();
        if (n < buf.size()) {
            buf.resize(n);
            return buf;
        }
        buf.resize(buf.size() * 2);
    }
}

// ProductVersion (M.m.p) from our own VERSIONINFO resource. version.dll is
// loaded at runtime so the stub's import table stays within the sanctioned
// five libraries.
std::wstring ownProductVersion() {
    using GetSizeFn = DWORD(APIENTRY*)(LPCWSTR, LPDWORD);
    using GetInfoFn = BOOL(APIENTRY*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(APIENTRY*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);

    const std::wstring self = moduleFileName();
    if (self.empty()) return L"unknown";
    HMODULE lib = LoadLibraryExW(L"version.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!lib) return L"unknown";
    std::wstring result = L"unknown";
    const GetSizeFn getSize =
        reinterpret_cast<GetSizeFn>(GetProcAddress(lib, "GetFileVersionInfoSizeW"));
    const GetInfoFn getInfo =
        reinterpret_cast<GetInfoFn>(GetProcAddress(lib, "GetFileVersionInfoW"));
    const QueryFn query = reinterpret_cast<QueryFn>(GetProcAddress(lib, "VerQueryValueW"));
    if (getSize && getInfo && query) {
        DWORD ignored = 0;
        const DWORD size = getSize(self.c_str(), &ignored);
        if (size > 0) {
            std::string block(size, '\0');
            if (getInfo(self.c_str(), 0, size, block.data())) {
                VS_FIXEDFILEINFO* fixed = nullptr;
                UINT fixedLen = 0;
                if (query(block.data(), L"\\", reinterpret_cast<LPVOID*>(&fixed), &fixedLen) &&
                    fixed && fixedLen >= sizeof(VS_FIXEDFILEINFO)) {
                    wchar_t text[64];
                    swprintf(text, 64, L"%u.%u.%u",
                             static_cast<unsigned>(HIWORD(fixed->dwProductVersionMS)),
                             static_cast<unsigned>(LOWORD(fixed->dwProductVersionMS)),
                             static_cast<unsigned>(HIWORD(fixed->dwProductVersionLS)));
                    result = text;
                }
            }
        }
    }
    FreeLibrary(lib);
    return result;
}

// OS gate: Windows 10 1809 (build 17763) or newer. RtlGetVersion reports the
// truth regardless of compatibility shims. ARM64 (x64 emulation) is allowed
// and logged; a 32-bit-only machine cannot even start this x64 image.
bool osSupported() {
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return false;
    const RtlGetVersionFn rtlGetVersion =
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtlGetVersion) return false;
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    if (rtlGetVersion(&info) != 0) return false;
    if (info.dwMajorVersion < 10) return false;
    if (info.dwMajorVersion == 10 && info.dwBuildNumber < 17763) return false;

    using IsWow64Process2Fn = BOOL(WINAPI*)(HANDLE, USHORT*, USHORT*);
    const IsWow64Process2Fn isWow64Process2 = reinterpret_cast<IsWow64Process2Fn>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "IsWow64Process2"));
    if (isWow64Process2) {
        USHORT processMachine = 0;
        USHORT nativeMachine = 0;
        if (isWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) &&
            nativeMachine == IMAGE_FILE_MACHINE_ARM64)
            debugLog(L"running under ARM64 x64 emulation (allowed)");
    }
    return true;
}

std::wstring tempDirectory() {
    std::wstring buf(32768, L'\0');
    const DWORD n = GetTempPathW(static_cast<DWORD>(buf.size()), buf.data());
    if (n == 0 || n >= buf.size()) return std::wstring();
    buf.resize(n); // keeps the trailing backslash GetTempPathW guarantees
    return buf;
}

std::wstring randomHex8() {
    unsigned char bytes[4] = {};
    if (!SystemFunction036(bytes, sizeof(bytes))) {
        // Uniqueness fallback; this is a temp-dir suffix, not key material.
        const ULONGLONG mix =
            GetTickCount64() ^ (static_cast<ULONGLONG>(GetCurrentProcessId()) << 17);
        std::memcpy(bytes, &mix, sizeof(bytes));
    }
    wchar_t text[9];
    swprintf(text, 9, L"%02x%02x%02x%02x", static_cast<unsigned>(bytes[0]),
             static_cast<unsigned>(bytes[1]), static_cast<unsigned>(bytes[2]),
             static_cast<unsigned>(bytes[3]));
    return text;
}

std::wstring withLongPathPrefix(const std::wstring& absolute) {
    if (absolute.rfind(L"\\\\?\\", 0) == 0) return absolute;
    if (absolute.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + absolute.substr(2);
    return L"\\\\?\\" + absolute;
}

bool removeTreeOnce(const std::wstring& dir) {
    const std::wstring prefixed = withLongPathPrefix(dir);
    WIN32_FIND_DATAW find{};
    const HANDLE h = FindFirstFileW((prefixed + L"\\*").c_str(), &find);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(find.cFileName, L".") == 0 || std::wcscmp(find.cFileName, L"..") == 0)
                continue;
            const std::wstring child = dir + L"\\" + find.cFileName;
            const std::wstring childPrefixed = withLongPathPrefix(child);
            if (find.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                removeTreeOnce(child);
            } else {
                if (find.dwFileAttributes & FILE_ATTRIBUTE_READONLY)
                    SetFileAttributesW(childPrefixed.c_str(), FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(childPrefixed.c_str());
            }
        } while (FindNextFileW(h, &find));
        FindClose(h);
    }
    return RemoveDirectoryW(prefixed.c_str()) != 0;
}

// Spec 1.3 step 8: 5 retries x 300 ms for AV stragglers; a stuck %TEMP% dir is
// disposable, so persistent failure is logged and tolerated.
void removeStagingWithRetries(const std::wstring& dir) {
    if (removeTreeOnce(dir)) return;
    for (int attempt = 0; attempt < 5; ++attempt) {
        Sleep(300);
        if (removeTreeOnce(dir)) return;
    }
    debugLog(L"staging cleanup incomplete; leaving the directory to %TEMP% hygiene");
}

struct ExtractJob {
    const wchar_t* exePath = nullptr;
    const payload::Trailer* trailer = nullptr;
    const wchar_t* destDir = nullptr;
    std::atomic<bool>* cancel = nullptr;
    std::atomic<bool> done{false};
    bool ok = false;
};

DWORD WINAPI extractThreadProc(LPVOID param) {
    ExtractJob* job = static_cast<ExtractJob*>(param);
    job->ok = payload::extractAll(job->exePath, *job->trailer, job->destDir, nullptr, nullptr,
                                  *job->cancel);
    job->done.store(true, std::memory_order_relaxed);
    return 0;
}

int runStub() {
    // 1. Argument scan. The stub understands only the mode-relevant flags and
    // hands EVERYTHING through to dish-setup-ui.exe verbatim.
    bool silent = false;
    bool wantHelp = false;
    bool wantVersion = false;
    int argc = 0;
    if (wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"/S") == 0 || _wcsicmp(argv[i], L"--silent") == 0 ||
                _wcsicmp(argv[i], L"--update-apply") == 0)
                silent = true;
            else if (_wcsicmp(argv[i], L"--help") == 0)
                wantHelp = true;
            else if (_wcsicmp(argv[i], L"--version") == 0)
                wantVersion = true;
        }
        LocalFree(argv);
    }
    if (wantHelp || wantVersion) {
        // Bare "M.m.p" so scripted callers can match the output exactly; the
        // value comes from the VERSIONINFO resource (single source
        // PROJECT_VERSION via packaging/setup/setup-stub.rc.in).
        if (wantVersion) writeToConsole((ownProductVersion() + L"\r\n").c_str());
        if (wantHelp) writeToConsole(kHelpText);
        return 0;
    }

    // 2. OS gate.
    if (!osSupported()) {
        if (!silent) stub::showOsTooOldDialog();
        return kExitUnsupportedOs;
    }

    // 3. Trailer + whole-payload CRC gate.
    const std::wstring selfPath = moduleFileName();
    if (selfPath.empty()) return kExitInternal;
    payload::Trailer trailer{};
    if (!payload::readTrailer(selfPath.c_str(), trailer) ||
        !payload::checkOverlayCrc(selfPath.c_str(), trailer)) {
        if (!silent) stub::showDamagedDialog();
        return kExitPayloadCorrupt;
    }

    // 4. %TEMP% preflight: room for the unpacked image plus margin.
    uint64_t imageBytes = 0;
    if (!payload::uncompressedTotal(selfPath.c_str(), trailer, imageBytes)) {
        if (!silent) stub::showDamagedDialog();
        return kExitPayloadCorrupt;
    }
    const std::wstring tempDir = tempDirectory();
    if (tempDir.empty()) return kExitInternal;
    ULARGE_INTEGER freeToCaller{};
    if (!GetDiskFreeSpaceExW(tempDir.c_str(), &freeToCaller, nullptr, nullptr))
        return kExitInternal;
    if (freeToCaller.QuadPart < imageBytes + kTempFreeSpaceMargin) {
        debugLog(L"not enough free space on the %TEMP% volume");
        return kExitDiskFull;
    }
    std::wstring staging;
    bool stagingCreated = false;
    for (int attempt = 0; attempt < 3 && !stagingCreated; ++attempt) {
        staging = tempDir + L"dish-setup-" + randomHex8();
        if (CreateDirectoryW(withLongPathPrefix(staging).c_str(), nullptr))
            stagingCreated = true;
        else if (GetLastError() != ERROR_ALREADY_EXISTS)
            break; // %TEMP% itself is broken; a new suffix will not help
    }
    if (!stagingCreated) return kExitInternal;

    // 5 + 6. Extract, behind the marquee unless silent. Cancel stops at the
    // next entry boundary.
    std::atomic<bool> cancel{false};
    ExtractJob job;
    job.exePath = selfPath.c_str();
    job.trailer = &trailer;
    job.destDir = staging.c_str();
    job.cancel = &cancel;
    if (silent) {
        job.ok = payload::extractAll(selfPath.c_str(), trailer, staging.c_str(), nullptr, nullptr,
                                     cancel);
        job.done.store(true, std::memory_order_relaxed);
    } else {
        const HANDLE worker = CreateThread(nullptr, 0, &extractThreadProc, &job, 0, nullptr);
        if (worker) {
            stub::runMarqueeUntil(job.done, cancel);
            WaitForSingleObject(worker, INFINITE);
            CloseHandle(worker);
        } else {
            job.ok = payload::extractAll(selfPath.c_str(), trailer, staging.c_str(), nullptr,
                                         nullptr, cancel);
            job.done.store(true, std::memory_order_relaxed);
        }
    }
    if (cancel.load(std::memory_order_relaxed)) {
        removeStagingWithRetries(staging);
        return kExitCancelled;
    }
    if (!job.ok) {
        removeStagingWithRetries(staging);
        if (!silent) stub::showDamagedDialog();
        return kExitPayloadCorrupt;
    }

    // 7. Hand off to the extracted wizard with the verbatim original tail.
    const std::wstring childExe = staging + L"\\dish-setup-ui.exe";
    if (GetFileAttributesW(withLongPathPrefix(childExe).c_str()) == INVALID_FILE_ATTRIBUTES) {
        removeStagingWithRetries(staging);
        if (!silent) stub::showDamagedDialog();
        return kExitPayloadCorrupt;
    }
    const std::wstring tail = commandLineTail();
    std::wstring commandLine = L"\"" + childExe + L"\" --staging \"" + staging +
                               L"\" --source-exe \"" + selfPath + L"\" --";
    if (!tail.empty()) {
        commandLine += L' ';
        commandLine += tail;
    }
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    // CreateProcessW may rewrite the buffer; std::wstring storage is writable.
    if (!CreateProcessW(childExe.c_str(), commandLine.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        nullptr, &startup, &process)) {
        debugLog(L"failed to start dish-setup-ui.exe from staging");
        removeStagingWithRetries(staging);
        if (!silent) stub::showDamagedDialog();
        return kExitInternal;
    }
    CloseHandle(process.hThread);
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD childCode = static_cast<DWORD>(kExitInternal);
    GetExitCodeProcess(process.hProcess, &childCode);
    CloseHandle(process.hProcess);

    // 8. Cleanup, then propagate the child's exit code.
    removeStagingWithRetries(staging);
    return static_cast<int>(childCode);
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) { return runStub(); }
