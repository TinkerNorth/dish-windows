// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/ops/Win32ProcessOps.h"

#include "installer/ops/Win32FileOps.h"

#include <QDir>
#include <QElapsedTimer>
#include <QThread>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#include <shellapi.h>
#include <tlhelp32.h>

#include <string>
#include <vector>

namespace dish::installer {

namespace {

// "\\?\C:\dir\" prefix form both sides so the compare is unambiguous at
// component boundaries and case-folded like NTFS.
QString canonicalPrefix(const QString& dir) {
    QString prefix = toExtendedPath(dir);
    if (!prefix.endsWith(QLatin1Char('\\'))) { prefix += QLatin1Char('\\'); }
    return prefix.toCaseFolded();
}

QString imagePathOf(HANDLE process) {
    wchar_t buffer[32768];
    DWORD size = static_cast<DWORD>(std::size(buffer));
    if (!QueryFullProcessImageNameW(process, 0, buffer, &size)) { return QString(); }
    return QString::fromWCharArray(buffer, static_cast<int>(size));
}

bool pathUnder(const QString& imagePath, const QString& foldedPrefix) {
    if (imagePath.isEmpty()) { return false; }
    return toExtendedPath(QDir::fromNativeSeparators(imagePath))
        .toCaseFolded()
        .startsWith(foldedPrefix);
}

struct CloseWindowsContext {
    DWORD pid = 0;
};

BOOL CALLBACK closeTopLevelWindows(HWND hwnd, LPARAM lparam) {
    const auto* ctx = reinterpret_cast<CloseWindowsContext*>(lparam);
    DWORD windowPid = 0;
    GetWindowThreadProcessId(hwnd, &windowPid);
    if (windowPid == ctx->pid && GetWindow(hwnd, GW_OWNER) == nullptr) {
        PostMessageW(hwnd, WM_CLOSE, 0, 0);
    }
    return TRUE;
}

bool pidGone(quint32 pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | SYNCHRONIZE, FALSE, pid);
    if (!process) {
        // No such pid (or no access — treat inaccessible as alive only when
        // it is provably running; invalid parameter means gone).
        return GetLastError() == ERROR_INVALID_PARAMETER;
    }
    const DWORD wait = WaitForSingleObject(process, 0);
    CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

bool allGone(const QVector<ProcInfo>& procs) {
    for (const ProcInfo& proc : procs) {
        if (!pidGone(proc.pid)) { return false; }
    }
    return true;
}

QString quoteArg(const QString& arg) {
    const bool needsQuotes = arg.isEmpty() || arg.contains(QLatin1Char(' ')) ||
                             arg.contains(QLatin1Char('\t')) || arg.contains(QLatin1Char('"'));
    if (!needsQuotes) { return arg; }
    QString quoted = QStringLiteral("\"");
    int backslashes = 0;
    for (const QChar ch : arg) {
        if (ch == QLatin1Char('\\')) {
            ++backslashes;
            continue;
        }
        if (ch == QLatin1Char('"')) {
            // Backslashes before a quote double, plus one to escape it.
            quoted += QString(backslashes * 2 + 1, QLatin1Char('\\'));
            quoted += QLatin1Char('"');
        } else {
            quoted += QString(backslashes, QLatin1Char('\\'));
            quoted += ch;
        }
        backslashes = 0;
    }
    // Trailing backslashes double so the closing quote stays a quote.
    quoted += QString(backslashes * 2, QLatin1Char('\\'));
    quoted += QLatin1Char('"');
    return quoted;
}

QString joinArgs(const QStringList& argv) {
    QString line;
    for (int i = 0; i < argv.size(); ++i) {
        if (i > 0) { line += QLatin1Char(' '); }
        line += quoteArg(argv.at(i));
    }
    return line;
}

} // namespace

QString buildCommandLine(const QString& exe, const QStringList& argv) {
    QString line = quoteArg(QDir::toNativeSeparators(exe));
    if (!argv.isEmpty()) {
        line += QLatin1Char(' ');
        line += joinArgs(argv);
    }
    return line;
}

QVector<ProcInfo> Win32ProcessOps::processesUnder(const QString& dir) {
    QVector<ProcInfo> result;
    const QString prefix = canonicalPrefix(dir);
    const DWORD ownPid = GetCurrentProcessId();

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { return result; }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    for (BOOL more = Process32FirstW(snapshot, &entry); more;
         more = Process32NextW(snapshot, &entry)) {
        if (entry.th32ProcessID == ownPid) { continue; }
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) { continue; }
        const QString imagePath = imagePathOf(process);
        CloseHandle(process);
        if (!pathUnder(imagePath, prefix)) { continue; }
        ProcInfo info;
        info.pid = entry.th32ProcessID;
        info.imagePath = QDir::fromNativeSeparators(imagePath);
        info.name = QString::fromWCharArray(entry.szExeFile);
        result.append(info);
    }
    CloseHandle(snapshot);
    return result;
}

bool Win32ProcessOps::requestClose(const QVector<ProcInfo>& procs, int timeoutMs) {
    for (const ProcInfo& proc : procs) {
        CloseWindowsContext ctx;
        ctx.pid = proc.pid;
        EnumWindows(closeTopLevelWindows, reinterpret_cast<LPARAM>(&ctx));
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (allGone(procs)) { return true; }
        QThread::msleep(250);
    }
    return allGone(procs);
}

bool Win32ProcessOps::terminate(const QVector<ProcInfo>& procs) {
    bool allOk = true;
    for (const ProcInfo& proc : procs) {
        if (pidGone(proc.pid)) { continue; }
        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, proc.pid);
        if (!process) {
            allOk = false;
            continue;
        }
        if (TerminateProcess(process, 1)) {
            WaitForSingleObject(process, 5000);
        } else {
            allOk = false;
        }
        CloseHandle(process);
    }
    return allOk && allGone(procs);
}

bool Win32ProcessOps::waitForPid(quint32 pid, int timeoutMs) {
    HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (!process) { return true; } // no such pid (or unwaitable): treat as exited
    const DWORD wait = WaitForSingleObject(process, static_cast<DWORD>(timeoutMs));
    CloseHandle(process);
    return wait == WAIT_OBJECT_0;
}

bool Win32ProcessOps::isElevated() {
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) { return false; }
    TOKEN_ELEVATION elevation{};
    DWORD size = 0;
    const bool ok =
        GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &size) != 0;
    CloseHandle(token);
    return ok && elevation.TokenIsElevated != 0;
}

OpResult Win32ProcessOps::relaunchElevated(const QString& exe, const QStringList& argv) {
    const QString params = joinArgs(argv);
    const std::wstring wideExe = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring wideParams = params.toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = wideExe.c_str();
    info.lpParameters = wideParams.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info)) {
        const DWORD lastError = GetLastError();
        const SetupError typed =
            lastError == ERROR_CANCELLED ? SetupError::NeedElevation : SetupError::Internal;
        return OpResult::failure(typed, exe, lastError);
    }
    if (info.hProcess) { CloseHandle(info.hProcess); }
    return OpResult::success(exe);
}

std::optional<int> Win32ProcessOps::runElevatedWait(const QString& exe, const QStringList& argv,
                                                    bool* declined) {
    if (declined) { *declined = false; }
    const QString params = joinArgs(argv);
    const std::wstring wideExe = QDir::toNativeSeparators(exe).toStdWString();
    const std::wstring wideParams = params.toStdWString();

    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"runas";
    info.lpFile = wideExe.c_str();
    info.lpParameters = wideParams.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info) || !info.hProcess) {
        if (declined && GetLastError() == ERROR_CANCELLED) { *declined = true; }
        return std::nullopt;
    }
    WaitForSingleObject(info.hProcess, INFINITE);
    DWORD exitCode = 1;
    GetExitCodeProcess(info.hProcess, &exitCode);
    CloseHandle(info.hProcess);
    return static_cast<int>(exitCode);
}

OpResult Win32ProcessOps::launchDetached(const QString& exe, const QStringList& argv,
                                         const QString& cwd, bool deElevate) {
    const QString line = buildCommandLine(exe, argv);
    std::wstring wideLine = line.toStdWString();
    const std::wstring wideCwd = QDir::toNativeSeparators(cwd).toStdWString();
    const wchar_t* cwdPtr = cwd.isEmpty() ? nullptr : wideCwd.c_str();

    if (deElevate && isElevated()) {
        // The explorer shell-window token is the invoking desktop user's
        // unelevated token; CreateProcessWithTokenW launches as them. When any
        // step fails the launch is SKIPPED — running the app elevated by
        // accident is worse than not launching (spec H6).
        HWND shell = GetShellWindow();
        DWORD shellPid = 0;
        if (shell) { GetWindowThreadProcessId(shell, &shellPid); }
        if (!shellPid) { return OpResult::failure(SetupError::Internal, exe); }
        HANDLE shellProcess = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, shellPid);
        if (!shellProcess) { return OpResult::failure(SetupError::Internal, exe, GetLastError()); }
        HANDLE shellToken = nullptr;
        HANDLE primary = nullptr;
        OpResult result = OpResult::success(exe);
        if (!OpenProcessToken(shellProcess, TOKEN_DUPLICATE, &shellToken)) {
            result = OpResult::failure(SetupError::Internal, exe, GetLastError());
        } else if (!DuplicateTokenEx(shellToken,
                                     TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE |
                                         TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                                     nullptr, SecurityImpersonation, TokenPrimary, &primary)) {
            result = OpResult::failure(SetupError::Internal, exe, GetLastError());
        } else {
            STARTUPINFOW startup{};
            startup.cb = sizeof(startup);
            PROCESS_INFORMATION process{};
            if (!CreateProcessWithTokenW(primary, 0, nullptr, wideLine.data(),
                                         CREATE_NEW_PROCESS_GROUP, nullptr, cwdPtr, &startup,
                                         &process)) {
                result = OpResult::failure(SetupError::Internal, exe, GetLastError());
            } else {
                CloseHandle(process.hThread);
                CloseHandle(process.hProcess);
            }
        }
        if (primary) { CloseHandle(primary); }
        if (shellToken) { CloseHandle(shellToken); }
        CloseHandle(shellProcess);
        return result;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, wideLine.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP | CREATE_DEFAULT_ERROR_MODE, nullptr, cwdPtr,
                        &startup, &process)) {
        return OpResult::failure(SetupError::Internal, exe, GetLastError());
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return OpResult::success(exe);
}

QStringList Win32ProcessOps::ownWorkingSetUnder(const QString& dir) {
    QStringList result;
    const QString prefix = canonicalPrefix(dir);
    HMODULE modules[1024];
    DWORD bytes = 0;
    if (!EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &bytes)) {
        return result;
    }
    const int count = static_cast<int>(bytes / sizeof(HMODULE));
    for (int i = 0; i < count; ++i) {
        wchar_t buffer[32768];
        const DWORD length = GetModuleFileNameExW(GetCurrentProcess(), modules[i], buffer,
                                                  static_cast<DWORD>(std::size(buffer)));
        if (length == 0) { continue; }
        const QString path =
            QDir::fromNativeSeparators(QString::fromWCharArray(buffer, static_cast<int>(length)));
        if (pathUnder(QDir::toNativeSeparators(path), prefix)) { result.append(path); }
    }
    return result;
}

} // namespace dish::installer
