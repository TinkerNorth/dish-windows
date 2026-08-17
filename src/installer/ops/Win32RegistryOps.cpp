// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/ops/Win32RegistryOps.h"

#include <QDate>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>

namespace dish::installer {

namespace {

const wchar_t* kArpSubkey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TinkerNorth.Dish";

HKEY hiveFor(Scope scope) {
    return scope == Scope::AllUsers ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
}

QString arpDisplayPath(Scope scope) {
    return (scope == Scope::AllUsers ? QStringLiteral("HKLM\\") : QStringLiteral("HKCU\\")) +
           QString::fromWCharArray(kArpSubkey);
}

LSTATUS setString(HKEY key, const wchar_t* name, const QString& value) {
    const std::wstring wide = value.toStdWString();
    return RegSetValueExW(key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(wide.c_str()),
                          static_cast<DWORD>((wide.size() + 1) * sizeof(wchar_t)));
}

LSTATUS setDword(HKEY key, const wchar_t* name, DWORD value) {
    return RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<const BYTE*>(&value),
                          sizeof(value));
}

std::optional<QString> readString(HKEY key, const wchar_t* name) {
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        type != REG_SZ || bytes == 0) {
        return std::nullopt;
    }
    std::wstring buffer(bytes / sizeof(wchar_t) + 1, L'\0');
    if (RegQueryValueExW(key, name, nullptr, nullptr, reinterpret_cast<BYTE*>(buffer.data()),
                         &bytes) != ERROR_SUCCESS) {
        return std::nullopt;
    }
    return QString::fromWCharArray(buffer.c_str());
}

} // namespace

OpResult Win32RegistryOps::writeArp(Scope scope, const ArpValues& values) {
    HKEY key = nullptr;
    const LSTATUS created =
        RegCreateKeyExW(hiveFor(scope), kArpSubkey, 0, nullptr, REG_OPTION_NON_VOLATILE,
                        KEY_SET_VALUE | KEY_WOW64_64KEY, nullptr, &key, nullptr);
    if (created != ERROR_SUCCESS) {
        return OpResult::failure(SetupError::RegistryFailed, arpDisplayPath(scope),
                                 static_cast<quint32>(created));
    }

    const QString installDate = values.installDate.isEmpty()
                                    ? QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))
                                    : values.installDate;
    LSTATUS status = ERROR_SUCCESS;
    const auto keep = [&status](LSTATUS s) {
        if (status == ERROR_SUCCESS) { status = s; }
    };
    keep(setString(key, L"DisplayName", values.displayName));
    keep(setString(key, L"DisplayVersion", values.displayVersion));
    keep(setDword(key, L"VersionMajor", static_cast<DWORD>(values.versionMajor)));
    keep(setDword(key, L"VersionMinor", static_cast<DWORD>(values.versionMinor)));
    keep(setString(key, L"Publisher", values.publisher));
    keep(setString(key, L"DisplayIcon", values.displayIcon));
    keep(setString(key, L"InstallLocation", values.installLocation));
    keep(setString(key, L"InstallDate", installDate));
    keep(setString(key, L"UninstallString", values.uninstallString));
    keep(setString(key, L"QuietUninstallString", values.quietUninstallString));
    keep(setDword(key, L"NoModify", 1));
    keep(setDword(key, L"NoRepair", 1));
    keep(setDword(key, L"EstimatedSize", values.estimatedSizeKiB));
    keep(setString(key, L"URLInfoAbout", values.urlInfoAbout));
    keep(setString(key, L"HelpLink", values.helpLink));
    keep(setString(key, L"InstallScope", values.installScope));
    RegCloseKey(key);

    if (status != ERROR_SUCCESS) {
        return OpResult::failure(SetupError::RegistryFailed, arpDisplayPath(scope),
                                 static_cast<quint32>(status));
    }
    return OpResult::success(arpDisplayPath(scope));
}

OpResult Win32RegistryOps::deleteArp(Scope scope) {
    const LSTATUS status = RegDeleteKeyExW(hiveFor(scope), kArpSubkey, KEY_WOW64_64KEY, 0);
    if (status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND ||
        status == ERROR_PATH_NOT_FOUND) {
        return OpResult::success(arpDisplayPath(scope)); // already gone = done
    }
    return OpResult::failure(SetupError::RegistryFailed, arpDisplayPath(scope),
                             static_cast<quint32>(status));
}

std::optional<InstalledInfo> Win32RegistryOps::readInstalled(Scope scope) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(hiveFor(scope), kArpSubkey, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) !=
        ERROR_SUCCESS) {
        return std::nullopt;
    }
    InstalledInfo info;
    const auto version = readString(key, L"DisplayVersion");
    const auto location = readString(key, L"InstallLocation");
    const auto scopeHint = readString(key, L"InstallScope");
    RegCloseKey(key);
    if (!version || !location) { return std::nullopt; }
    info.displayVersion = *version;
    info.installLocation = *location;
    info.installScope = scopeHint.value_or(scopeToken(scope));
    return info;
}

void Win32RegistryOps::purgeUserSettingsTrees() {
    // The cross-client hive and the Windows-shell hive (PRIVACY.md section 2.1).
    for (const wchar_t* subkey : {L"Software\\Dish\\Dish", L"Software\\TinkerNorth\\Dish"}) {
        RegDeleteTreeW(HKEY_CURRENT_USER, subkey);
    }
    // Prune the vendor parents when the purge left them empty (best-effort;
    // fails harmlessly when other products share them).
    for (const wchar_t* subkey : {L"Software\\Dish", L"Software\\TinkerNorth"}) {
        RegDeleteKeyExW(HKEY_CURRENT_USER, subkey, 0, 0);
    }
}

} // namespace dish::installer
