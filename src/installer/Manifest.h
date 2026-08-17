// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The two JSON manifests of spec sections 1.2 and 3.3. PayloadManifest is
// manifest.json inside the extracted install image (written by
// dish-payload-pack, the single source of truth for copy, verify, progress,
// EstimatedSize and uninstall). InstalledManifest is .dish-manifest.json in the
// install dir (the recorded choices an upgrade and the uninstaller read).
// Parsing is strict: a manifest that fails any structural rule yields nullopt,
// never a half-trusted object.

#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

namespace dish::installer {

// Entry paths are installed-relative, ASCII, forward slashes, no "..", no
// drive letters, no leading or trailing slash. Re-validated here even though
// the pack tool enforces the same rules, because a manifest can arrive from a
// tampered image.
bool isSafeRelativePath(const QString& path);

// A recorded shortcut path the uninstaller may DELETE. The installer only ever
// records shortcutLinkPath(location, scope), i.e. "<known folder>/Dish.lnk", so
// the check is: absolute (drive or UNC), no "..", and a final component of
// exactly "Dish.lnk". The recorded string is still the authority on WHICH
// directory (a known folder that moved after the install must keep resolving to
// where the record says, see UpdateApply's shortcutWasDeleted) — this only stops
// a hand-edited .dish-manifest.json from pointing the delete at an arbitrary
// file, which for a machine-scope record runs elevated.
bool isSafeShortcutPath(const QString& path);

struct PayloadEntry {
    QString path;     // installed relative path
    QString stagedAs; // source name inside the extracted image; == path unless aliased
    qint64 size = 0;
    QByteArray sha256Hex; // 64 lowercase hex chars
    bool operator==(const PayloadEntry& o) const {
        return path == o.path && stagedAs == o.stagedAs && size == o.size &&
               sha256Hex == o.sha256Hex;
    }
    bool operator!=(const PayloadEntry& o) const { return !(*this == o); }
};

struct PayloadManifest {
    int schema = 1;
    QString version;
    qint64 totalBytes = 0;
    QVector<PayloadEntry> files;

    // Rejects: schema != 1, malformed version, unsafe or duplicate paths,
    // negative sizes, malformed hashes, and totalBytes that disagrees with the
    // sum of entry sizes (byte-accurate progress depends on it).
    static std::optional<PayloadManifest> fromJson(const QByteArray& json);
    QByteArray toJson() const; // stable ordering, LF
    bool operator==(const PayloadManifest& o) const {
        return schema == o.schema && version == o.version && totalBytes == o.totalBytes &&
               files == o.files;
    }
    bool operator!=(const PayloadManifest& o) const { return !(*this == o); }
};

struct InstalledManifest { // .dish-manifest.json in install dir
    int schema = 1;
    QString version;
    QString installDir;
    QString scope; // "user" | "machine"
    bool startMenu = true;
    bool desktop = false;
    QStringList shortcutPaths; // absolute .lnk paths created
    QString installedUtc;
    QVector<PayloadEntry> files; // installed set (stagedAs == path throughout)

    static std::optional<InstalledManifest> fromJson(const QByteArray& json);
    QByteArray toJson() const; // stable ordering, LF
    bool operator==(const InstalledManifest& o) const {
        return schema == o.schema && version == o.version && installDir == o.installDir &&
               scope == o.scope && startMenu == o.startMenu && desktop == o.desktop &&
               shortcutPaths == o.shortcutPaths && installedUtc == o.installedUtc &&
               files == o.files;
    }
    bool operator!=(const InstalledManifest& o) const { return !(*this == o); }
};

} // namespace dish::installer
