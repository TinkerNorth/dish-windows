// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The user's (or the CLI's) resolved install choices, plus the destination
// deny-list. The plan is data: reducers consume it, coordinators execute it,
// and CliOptions round-trips it through an elevation relaunch.

#pragma once

#include <QDir>
#include <QFileInfo>
#include <QString>

#include <optional>

namespace dish::installer {

enum class Scope { PerUser, AllUsers };
enum class ClosePolicy { Abort, Graceful, Force }; // silent gate policy

// Registry / manifest / CLI spelling of Scope. Kept here so every writer and
// parser shares one vocabulary ("user" | "machine").
inline QString scopeToken(Scope scope) {
    return scope == Scope::AllUsers ? QStringLiteral("machine") : QStringLiteral("user");
}
inline std::optional<Scope> scopeFromToken(const QString& token) {
    if (token.compare(QStringLiteral("user"), Qt::CaseInsensitive) == 0) { return Scope::PerUser; }
    if (token.compare(QStringLiteral("machine"), Qt::CaseInsensitive) == 0) {
        return Scope::AllUsers;
    }
    return std::nullopt;
}

struct InstallPlan {
    Scope scope = Scope::PerUser;
    QString installDir;
    bool startMenu = true;
    bool desktop = false;
    bool launch = false;
    bool allowDowngrade = false;
    ClosePolicy closePolicy = ClosePolicy::Abort;
    bool isUpgrade = false;
    QString existingVersion;
    QString existingDir;

    bool operator==(const InstallPlan& o) const {
        return scope == o.scope && installDir == o.installDir && startMenu == o.startMenu &&
               desktop == o.desktop && launch == o.launch && allowDowngrade == o.allowDowngrade &&
               closePolicy == o.closePolicy && isUpgrade == o.isUpgrade &&
               existingVersion == o.existingVersion && existingDir == o.existingDir;
    }
    bool operator!=(const InstallPlan& o) const { return !(*this == o); }
};

enum class DirStatus { Ok, NotAbsolute, Denied, Invalid, IsSystem, NotEmpty, IsExistingInstall };

// The recorded-install marker; presence makes a directory an upgrade target
// rather than a denied non-empty destination.
inline QString installedManifestFileName() { return QStringLiteral(".dish-manifest.json"); }

// The two-phase-upgrade working directories inside the install dir (spec 11.2):
// new files stage into `.dish-stage`, displaced files back up into `.dish-old`
// until CommitCleanup deletes both.
inline QString stageDirName() { return QStringLiteral(".dish-stage"); }
inline QString oldDirName() { return QStringLiteral(".dish-old"); }

// Deny-list validation. Deny: relative paths, drive roots, %WINDIR% and below,
// the Program Files roots themselves, the user-profile root itself. Accept:
// nonexistent dirs, empty dirs, and dirs holding .dish-manifest.json (existing
// installs). Everything filesystem-shaped but unacceptable maps to a typed
// status so the UI can phrase each case; nothing here throws.
inline DirStatus validateInstallDir(const QString& dir) {
    const QString trimmed = dir.trimmed();
    if (trimmed.isEmpty()) { return DirStatus::Invalid; }

    // Forward-slash canonical form for all comparisons below.
    QString clean = QDir::cleanPath(QDir::fromNativeSeparators(trimmed));
    if (clean.isEmpty()) { return DirStatus::Invalid; }

    const bool driveAbsolute = clean.size() >= 2 && clean[1] == QLatin1Char(':') &&
                               ((clean[0] >= QLatin1Char('a') && clean[0] <= QLatin1Char('z')) ||
                                (clean[0] >= QLatin1Char('A') && clean[0] <= QLatin1Char('Z')));
    const bool uncAbsolute = clean.startsWith(QStringLiteral("//"));
    if (!driveAbsolute && !uncAbsolute) { return DirStatus::NotAbsolute; }
    if (driveAbsolute && clean.size() >= 3 && clean[2] != QLatin1Char('/')) {
        return DirStatus::Invalid; // "C:foo" drive-relative form
    }

    // Characters Windows cannot put in a path component. ':' is legal only as
    // the drive separator already consumed above.
    const QString body = driveAbsolute ? clean.mid(2) : clean;
    for (const QChar ch : body) {
        if (ch == QLatin1Char('<') || ch == QLatin1Char('>') || ch == QLatin1Char('"') ||
            ch == QLatin1Char('|') || ch == QLatin1Char('?') || ch == QLatin1Char('*') ||
            ch == QLatin1Char(':') || ch.unicode() < 0x20) {
            return DirStatus::Invalid;
        }
    }

    const QString folded = clean.toCaseFolded();
    const auto foldedOf = [](const QString& env) {
        return QDir::cleanPath(QDir::fromNativeSeparators(env)).toCaseFolded();
    };
    const auto isOrUnder = [&folded](const QString& root) {
        return !root.isEmpty() && (folded == root || folded.startsWith(root + QLatin1Char('/')));
    };

    if (driveAbsolute && clean.size() <= 3) { return DirStatus::Denied; } // "C:/"
    if (uncAbsolute) {
        // "//server/share" itself is a root; deeper is fine.
        if (clean.count(QLatin1Char('/')) <= 3) { return DirStatus::Denied; }
    }

    if (isOrUnder(foldedOf(qEnvironmentVariable("WINDIR")))) { return DirStatus::IsSystem; }
    for (const char* var : {"ProgramFiles", "ProgramFiles(x86)", "ProgramW6432"}) {
        const QString root = foldedOf(qEnvironmentVariable(var));
        if (!root.isEmpty() && folded == root) { return DirStatus::Denied; }
    }
    {
        const QString profile = foldedOf(qEnvironmentVariable("USERPROFILE"));
        if (!profile.isEmpty() && folded == profile) { return DirStatus::Denied; }
    }

    const QFileInfo info(clean);
    if (!info.exists()) { return DirStatus::Ok; }
    if (!info.isDir()) { return DirStatus::Invalid; }
    if (QFileInfo::exists(clean + QLatin1Char('/') + installedManifestFileName())) {
        return DirStatus::IsExistingInstall;
    }
    if (!QDir(clean).isEmpty()) { return DirStatus::NotEmpty; }
    return DirStatus::Ok;
}

} // namespace dish::installer
