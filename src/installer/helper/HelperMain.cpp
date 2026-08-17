// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// uninstall-helper.exe: the one-hop janitor that finishes what uninstall.exe
// cannot, because uninstall.exe is the file being deleted (spec 11.1 step 8).
//
// UninstallCoordinator copies this binary into %TEMP%\dish-uninstall-<hex>\,
// writes the leftover paths it could not remove (its own exe, this helper's
// source copy, the mapped DLL closure, anything an AV or indexer had open)
// into a UTF-16LE list file, and spawns us:
//
//   uninstall-helper.exe --waitpid <pid> --list <file> --dir <installdir>
//                        --arp user|machine [--result <file>]
//
// Then, in order:
//   1. wait for that pid to exit, so nothing is still mapped;
//   2. delete every listed path, retrying with exponential backoff for up to
//      30 s (AV scanners hold handles for seconds after a process dies);
//   3. prune the install directory bottom-up and remove it if it came out
//      empty;
//   4. delete the ARP key LAST, and only if <dir>\uninstall.exe is really
//      gone. A half-finished cleanup therefore leaves an entry in Installed
//      apps that still points at a working uninstaller, which the user can
//      simply run again;
//   5. hand our own temp directory to a windowless cmd.exe that waits ~2 s and
//      removes it. No MoveFileEx(DELAY_UNTIL_REBOOT) (needs admin, fails
//      invisibly for a per-user install) and no self-deletion tricks, which
//      are exactly the pattern AV heuristics are tuned to flag.
//
// /MT, no Qt, no CRT DLLs: by the time this runs, the CRT the install shipped
// app-locally is one of the files it was asked to delete.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h> // CommandLineToArgvW

#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <string>
#include <vector>

namespace {

// Exit codes: 0 clean, 1 residue or a failed step, 2 usage. The section 9
// table governs the setup binaries; nothing reads this process's code except a
// human in a log, and --result carries the same answer in text.
constexpr int kExitOk = 0;
constexpr int kExitResidue = 1;
constexpr int kExitUsage = 2;

constexpr wchar_t kArpSubKey[] =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TinkerNorth.Dish";

// 250 ms doubling, 30 s of patience in total. Long enough for a real-time
// scanner to let go of a just-terminated process's image, short enough that a
// genuinely stuck file does not keep a background process alive for minutes.
constexpr DWORD kRetryDelaysMs[] = {250, 500, 1000, 2000, 4000, 8000, 14000};
constexpr int kRetryRounds = static_cast<int>(sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]));

constexpr DWORD kWaitTimeoutMs = 120000;

void debugLine(const wchar_t* text) {
    OutputDebugStringW(L"[dish uninstall-helper] ");
    OutputDebugStringW(text);
    OutputDebugStringW(L"\r\n");
}

// Win32 refuses relative and >MAX_PATH paths without the extended prefix, and
// the paths in the list file come straight from a user-chosen install
// directory. UNC gets its own spelling.
std::wstring extended(const std::wstring& path) {
    if (path.size() < 4) { return path; }
    if (path.compare(0, 4, L"\\\\?\\") == 0) { return path; }
    if (path.compare(0, 2, L"\\\\") == 0) { return L"\\\\?\\UNC" + path.substr(1); }
    if (path.size() >= 3 && path[1] == L':' && path[2] == L'\\') { return L"\\\\?\\" + path; }
    return path;
}

bool exists(const std::wstring& path) {
    return GetFileAttributesW(extended(path).c_str()) != INVALID_FILE_ATTRIBUTES;
}

bool isDirectory(const std::wstring& path) {
    const DWORD attributes = GetFileAttributesW(extended(path).c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

// True when the entry is gone after this call, including "was never there".
bool tryRemove(const std::wstring& path) {
    const std::wstring wide = extended(path);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) { return true; }
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0) {
        SetFileAttributesW(wide.c_str(), attributes & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
    }
    if ((attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        return RemoveDirectoryW(wide.c_str()) != 0;
    }
    return DeleteFileW(wide.c_str()) != 0;
}

// Depth-first: children first, so a directory whose whole subtree was listed
// disappears in one pass. Returns true when `dir` itself is gone afterwards.
bool pruneEmptyTree(const std::wstring& dir) {
    const std::wstring pattern = extended(dir) + L"\\*";
    WIN32_FIND_DATAW found{};
    const HANDLE handle = FindFirstFileW(pattern.c_str(), &found);
    if (handle == INVALID_HANDLE_VALUE) {
        // No such directory is the outcome we wanted; anything else stays.
        return GetFileAttributesW(extended(dir).c_str()) == INVALID_FILE_ATTRIBUTES;
    }
    bool empty = true;
    do {
        const std::wstring name = found.cFileName;
        if (name == L"." || name == L"..") { continue; }
        const std::wstring child = dir + L"\\" + name;
        if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            if (!pruneEmptyTree(child)) { empty = false; }
        } else {
            empty = false;
        }
    } while (FindNextFileW(handle, &found) != 0);
    FindClose(handle);
    if (!empty) { return false; }
    return RemoveDirectoryW(extended(dir).c_str()) != 0;
}

// The list is what UninstallCoordinator wrote: UTF-16LE, BOM, CRLF-separated,
// native separators, one absolute path per line.
std::vector<std::wstring> readList(const std::wstring& path) {
    std::vector<std::wstring> lines;
    const HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { return lines; }
    LARGE_INTEGER size{};
    if (GetFileSizeEx(file, &size) == 0 || size.QuadPart <= 0 || size.QuadPart > (4 << 20)) {
        CloseHandle(file);
        return lines;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr);
    CloseHandle(file);
    if (ok == 0 || read != bytes.size()) { return lines; }

    std::wstring text(reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / 2);
    if (!text.empty() && text.front() == L'\uFEFF') { text.erase(0, 1); }
    size_t start = 0;
    while (start <= text.size()) {
        const size_t end = text.find(L'\n', start);
        std::wstring line = text.substr(start, end == std::wstring::npos ? end : end - start);
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ')) { line.pop_back(); }
        if (!line.empty()) { lines.push_back(line); }
        if (end == std::wstring::npos) { break; }
        start = end + 1;
    }
    return lines;
}

bool deleteArpKey(bool machineScope) {
    const HKEY hive = machineScope ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    // KEY_WOW64_64KEY: the helper is a 64-bit process today, but the flag is
    // what makes the view explicit rather than inherited.
    const LSTATUS status = RegDeleteKeyExW(hive, kArpSubKey, KEY_WOW64_64KEY, 0);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}

void writeResult(const std::wstring& path, const char* token, size_t residue) {
    if (path.empty()) { return; }
    char buffer[64];
    const int length = _snprintf_s(buffer, sizeof(buffer), _TRUNCATE, "%s %zu\n", token, residue);
    if (length <= 0) { return; }
    const HANDLE file = CreateFileW(extended(path).c_str(), GENERIC_WRITE, 0, nullptr,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { return; }
    DWORD written = 0;
    WriteFile(file, buffer, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(file);
}

std::wstring ownDirectory() {
    wchar_t buffer[MAX_PATH * 2];
    const DWORD length = GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) { return std::wstring(); }
    std::wstring path(buffer, length);
    const size_t slash = path.find_last_of(L'\\');
    if (slash == std::wstring::npos) { return std::wstring(); }
    return path.substr(0, slash);
}

// cmd.exe outlives us and removes the directory this exe is running from.
// `ping -n 3` is the portable two-second sleep that exists on every SKU;
// timeout.exe reads the console input handle, which a windowless child has no
// usable one of.
void scheduleSelfCleanup(const std::wstring& helperDir) {
    if (helperDir.empty()) { return; }
    wchar_t comspec[MAX_PATH];
    if (GetEnvironmentVariableW(L"ComSpec", comspec, MAX_PATH) == 0) {
        wcscpy_s(comspec, L"C:\\Windows\\System32\\cmd.exe");
    }
    std::wstring command = L"\"";
    command += comspec;
    command += L"\" /c ping 127.0.0.1 -n 3 >nul & rd /s /q \"";
    command += helperDir;
    command += L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    // CREATE_NO_WINDOW alone, never OR-ed with DETACHED_PROCESS: the two do not
    // combine, and CreateProcessW documents CREATE_NO_WINDOW as ignored when
    // DETACHED_PROCESS is also set. Passing both therefore asked for exactly
    // what it was trying to avoid. A detached cmd.exe starts owning no console,
    // allocates a fresh visible one the moment it touches console I/O, and the
    // uninstall ends with a black window flashing on screen for two seconds.
    // CREATE_NO_WINDOW gives it a console that is created hidden instead.
    // We are /SUBSYSTEM:WINDOWS and hold no console of our own, so there was
    // never an inherited one for DETACHED_PROCESS to detach from.
    if (CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                       nullptr, L"C:\\", &startup, &process) != 0) {
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
    } else {
        // One namespaced directory under %TEMP% is harmless and logged;
        // Storage Sense collects it.
        debugLine(L"self-cleanup could not start; leaving the temp directory");
    }
}

struct Options {
    DWORD waitPid = 0;
    std::wstring listPath;
    std::wstring installDir;
    std::wstring resultPath;
    bool machineScope = false;
    bool valid = false;
};

Options parse(int argc, wchar_t** argv) {
    Options options;
    bool sawArp = false;
    for (int i = 1; i < argc; ++i) {
        const std::wstring flag = argv[i];
        const bool hasValue = (i + 1) < argc;
        if (flag == L"--waitpid" && hasValue) {
            options.waitPid = static_cast<DWORD>(_wtoi64(argv[++i]));
        } else if (flag == L"--list" && hasValue) {
            options.listPath = argv[++i];
        } else if (flag == L"--dir" && hasValue) {
            options.installDir = argv[++i];
        } else if (flag == L"--result" && hasValue) {
            options.resultPath = argv[++i];
        } else if (flag == L"--arp" && hasValue) {
            const std::wstring scope = argv[++i];
            if (scope != L"user" && scope != L"machine") { return options; }
            options.machineScope = (scope == L"machine");
            sawArp = true;
        } else {
            return options;
        }
    }
    options.valid = options.waitPid != 0 && !options.installDir.empty() && sawArp;
    return options;
}

int run(const Options& options) {
    // 1. Nothing may be mapped while we delete. A vanished pid is the normal
    // case (uninstall.exe usually exits before we get scheduled).
    if (const HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, options.waitPid)) {
        WaitForSingleObject(process, kWaitTimeoutMs);
        CloseHandle(process);
    }

    // 2. The leftovers, retried while an AV holds them.
    std::vector<std::wstring> pending = readList(options.listPath);
    if (!options.listPath.empty()) { pending.push_back(options.listPath); }
    for (int round = 0; round <= kRetryRounds && !pending.empty(); ++round) {
        if (round > 0) { Sleep(kRetryDelaysMs[round - 1]); }
        std::vector<std::wstring> stillPending;
        for (const std::wstring& path : pending) {
            if (!tryRemove(path)) { stillPending.push_back(path); }
        }
        pending.swap(stillPending);
    }

    // 3. Whatever is left keeps its parent alive; prune what does not.
    pruneEmptyTree(options.installDir);

    // 4. ARP last, and only when the uninstaller it points at is really gone.
    const std::wstring uninstaller = options.installDir + L"\\uninstall.exe";
    bool arpRemoved = false;
    if (!exists(uninstaller)) {
        arpRemoved = deleteArpKey(options.machineScope);
        if (!arpRemoved) { debugLine(L"ARP key could not be deleted"); }
    } else {
        debugLine(L"uninstall.exe survived; leaving a retryable ARP entry");
    }

    const bool clean = pending.empty() && !isDirectory(options.installDir) && arpRemoved;
    writeResult(options.resultPath, clean ? "ok" : "residue", pending.size());

    // 5. Our own temp directory, unless we are somehow running from the
    // install directory we were told to clear.
    const std::wstring helperDir = ownDirectory();
    if (!helperDir.empty() && _wcsicmp(helperDir.c_str(), options.installDir.c_str()) != 0) {
        scheduleSelfCleanup(helperDir);
    }

    return clean ? kExitOk : kExitResidue;
}

} // namespace

int APIENTRY wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) { return kExitUsage; }
    const Options options = parse(argc, argv);
    LocalFree(argv);
    if (!options.valid) {
        debugLine(L"usage: uninstall-helper --waitpid <pid> --list <file> --dir <dir> "
                  L"--arp user|machine [--result <file>]");
        return kExitUsage;
    }
    return run(options);
}
