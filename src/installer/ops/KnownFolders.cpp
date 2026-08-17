// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/ops/KnownFolders.h"

#include <QDir>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <knownfolders.h>
#include <shlobj.h>

namespace dish::installer {

namespace {

QString knownFolder(const KNOWNFOLDERID& id) {
    PWSTR raw = nullptr;
    const HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw);
    QString path;
    if (SUCCEEDED(hr) && raw) { path = QDir::fromNativeSeparators(QString::fromWCharArray(raw)); }
    if (raw) { CoTaskMemFree(raw); }
    return path;
}

} // namespace

QString localAppDataDir() {
    const QString env = qEnvironmentVariable("LOCALAPPDATA");
    if (!env.isEmpty()) { return QDir::fromNativeSeparators(env); }
    return knownFolder(FOLDERID_LocalAppData);
}

QString programsDir(Scope scope) {
    return knownFolder(scope == Scope::AllUsers ? FOLDERID_CommonPrograms : FOLDERID_Programs);
}

QString desktopDir(Scope scope) {
    return knownFolder(scope == Scope::AllUsers ? FOLDERID_PublicDesktop : FOLDERID_Desktop);
}

QString shortcutLinkPath(ShortcutLocation location, Scope scope) {
    const QString dir =
        location == ShortcutLocation::Desktop ? desktopDir(scope) : programsDir(scope);
    if (dir.isEmpty()) { return QString(); }
    return dir + QStringLiteral("/Dish.lnk");
}

QString defaultInstallDir(Scope scope) {
    if (scope == Scope::AllUsers) {
        const QString programFiles = knownFolder(FOLDERID_ProgramFilesX64);
        if (programFiles.isEmpty()) { return QString(); }
        return programFiles + QStringLiteral("/Dish");
    }
    // FOLDERID_UserProgramFiles is %LOCALAPPDATA%\Programs; it may not exist as
    // a directory yet on a fresh profile, which is fine — the install creates it.
    QString userPrograms = knownFolder(FOLDERID_UserProgramFiles);
    if (userPrograms.isEmpty()) {
        const QString localAppData = localAppDataDir();
        if (localAppData.isEmpty()) { return QString(); }
        userPrograms = localAppData + QStringLiteral("/Programs");
    }
    return userPrograms + QStringLiteral("/Dish");
}

QString updatesCacheDir() {
    const QString localAppData = localAppDataDir();
    if (localAppData.isEmpty()) { return QString(); }
    return localAppData + QStringLiteral("/Dish/updates");
}

QString tempDir() {
    wchar_t buffer[MAX_PATH + 2] = {};
    const DWORD length = GetTempPathW(MAX_PATH + 1, buffer);
    if (length == 0 || length > MAX_PATH + 1) { return QString(); }
    QString path = QDir::fromNativeSeparators(QString::fromWCharArray(buffer, length));
    while (path.endsWith(QLatin1Char('/'))) { path.chop(1); }
    return path;
}

} // namespace dish::installer
