// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/Manifest.h"

#include "installer/InstallPlan.h"
#include "installer/VersionCompare.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

namespace dish::installer {

namespace {

bool isLowercaseHex64(const QByteArray& hex) {
    if (hex.size() != 64) { return false; }
    for (const char c : hex) {
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!digit && !lower) { return false; }
    }
    return true;
}

// Shared by both manifests. `allowAlias` admits the stagedAs indirection only
// where it is meaningful (the payload manifest); the installed manifest never
// carries one.
std::optional<QVector<PayloadEntry>> parseFiles(const QJsonValue& value, bool allowAlias) {
    if (!value.isArray()) { return std::nullopt; }
    QVector<PayloadEntry> files;
    QSet<QString> seen;
    const QJsonArray array = value.toArray();
    files.reserve(array.size());
    for (const QJsonValue& item : array) {
        if (!item.isObject()) { return std::nullopt; }
        const QJsonObject obj = item.toObject();
        PayloadEntry entry;
        entry.path = obj.value(QLatin1String("path")).toString();
        if (!isSafeRelativePath(entry.path)) { return std::nullopt; }
        if (seen.contains(entry.path)) { return std::nullopt; }
        seen.insert(entry.path);
        if (obj.contains(QLatin1String("stagedAs"))) {
            if (!allowAlias) { return std::nullopt; }
            entry.stagedAs = obj.value(QLatin1String("stagedAs")).toString();
            if (!isSafeRelativePath(entry.stagedAs)) { return std::nullopt; }
        } else {
            entry.stagedAs = entry.path;
        }
        const QJsonValue size = obj.value(QLatin1String("size"));
        if (!size.isDouble()) { return std::nullopt; }
        entry.size = static_cast<qint64>(size.toDouble());
        if (entry.size < 0) { return std::nullopt; }
        entry.sha256Hex = obj.value(QLatin1String("sha256")).toString().toLatin1();
        if (!isLowercaseHex64(entry.sha256Hex)) { return std::nullopt; }
        files.append(entry);
    }
    return files;
}

QJsonArray filesToJson(const QVector<PayloadEntry>& files, bool withAlias) {
    QJsonArray array;
    for (const PayloadEntry& entry : files) {
        QJsonObject obj;
        obj.insert(QLatin1String("path"), entry.path);
        if (withAlias && !entry.stagedAs.isEmpty() && entry.stagedAs != entry.path) {
            obj.insert(QLatin1String("stagedAs"), entry.stagedAs);
        }
        obj.insert(QLatin1String("size"), static_cast<double>(entry.size));
        obj.insert(QLatin1String("sha256"), QString::fromLatin1(entry.sha256Hex));
        array.append(obj);
    }
    return array;
}

// QJsonDocument::toJson emits keys alphabetically and indented; both manifests
// go through here so "stable ordering, LF" is one code path.
QByteArray toStableJson(const QJsonObject& root) {
    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);
    json.replace("\r\n", "\n");
    if (!json.endsWith('\n')) { json.append('\n'); }
    return json;
}

} // namespace

bool isSafeRelativePath(const QString& path) {
    if (path.isEmpty() || path.size() > 4096) { return false; }
    for (const QChar ch : path) {
        const char16_t u = ch.unicode();
        if (u < 0x20 || u > 0x7e) { return false; } // ASCII printable only
        if (u == u'\\' || u == u':' || u == u'<' || u == u'>' || u == u'"' || u == u'|' ||
            u == u'?' || u == u'*') {
            return false;
        }
    }
    if (path.startsWith(QLatin1Char('/')) || path.endsWith(QLatin1Char('/'))) { return false; }
    const QStringList parts = path.split(QLatin1Char('/'));
    for (const QString& part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String("..")) {
            return false;
        }
        // Windows silently strips trailing dots and spaces, which would make
        // the verified name and the created name disagree.
        if (part.endsWith(QLatin1Char(' ')) || part.endsWith(QLatin1Char('.'))) { return false; }
    }
    return true;
}

bool isSafeShortcutPath(const QString& path) {
    if (path.isEmpty() || path.size() > 4096) { return false; }
    for (const QChar ch : path) {
        const char16_t u = ch.unicode();
        if (u < 0x20 || u == u'<' || u == u'>' || u == u'"' || u == u'|' || u == u'?' ||
            u == u'*') {
            return false;
        }
    }
    const QString clean = QDir::cleanPath(QDir::fromNativeSeparators(path));
    // Absolute in the two forms Windows has. cleanPath has already collapsed
    // "..", so anything still carrying one could not be resolved and is out.
    const bool driveAbsolute = clean.size() >= 3 && clean.at(1) == QLatin1Char(':') &&
                               clean.at(2) == QLatin1Char('/') && clean.at(0).isLetter();
    const bool uncAbsolute = clean.startsWith(QStringLiteral("//"));
    if (!driveAbsolute && !uncAbsolute) { return false; }
    if (clean.split(QLatin1Char('/')).contains(QLatin1String(".."))) { return false; }
    const int slash = clean.lastIndexOf(QLatin1Char('/'));
    if (slash < 0) { return false; }
    return clean.mid(slash + 1).compare(QLatin1String("Dish.lnk"), Qt::CaseInsensitive) == 0;
}

std::optional<PayloadManifest> PayloadManifest::fromJson(const QByteArray& json) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { return std::nullopt; }
    const QJsonObject root = doc.object();

    PayloadManifest manifest;
    if (root.value(QLatin1String("schema")).toInt(-1) != 1) { return std::nullopt; }
    manifest.schema = 1;
    manifest.version = root.value(QLatin1String("version")).toString();
    if (!parseSemVer(manifest.version)) { return std::nullopt; }
    const QJsonValue total = root.value(QLatin1String("totalBytes"));
    if (!total.isDouble()) { return std::nullopt; }
    manifest.totalBytes = static_cast<qint64>(total.toDouble());
    if (manifest.totalBytes < 0) { return std::nullopt; }

    auto files = parseFiles(root.value(QLatin1String("files")), /*allowAlias=*/true);
    if (!files) { return std::nullopt; }
    manifest.files = std::move(*files);

    qint64 sum = 0;
    for (const PayloadEntry& entry : manifest.files) { sum += entry.size; }
    if (sum != manifest.totalBytes) { return std::nullopt; }
    return manifest;
}

QByteArray PayloadManifest::toJson() const {
    QJsonObject root;
    root.insert(QLatin1String("schema"), schema);
    root.insert(QLatin1String("version"), version);
    root.insert(QLatin1String("totalBytes"), static_cast<double>(totalBytes));
    root.insert(QLatin1String("files"), filesToJson(files, /*withAlias=*/true));
    return toStableJson(root);
}

std::optional<InstalledManifest> InstalledManifest::fromJson(const QByteArray& json) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { return std::nullopt; }
    const QJsonObject root = doc.object();

    InstalledManifest manifest;
    if (root.value(QLatin1String("schema")).toInt(-1) != 1) { return std::nullopt; }
    manifest.schema = 1;
    manifest.version = root.value(QLatin1String("version")).toString();
    if (!parseSemVer(manifest.version)) { return std::nullopt; }
    manifest.installDir = root.value(QLatin1String("installDir")).toString();
    if (manifest.installDir.isEmpty()) { return std::nullopt; }
    manifest.scope = root.value(QLatin1String("scope")).toString();
    if (!scopeFromToken(manifest.scope)) { return std::nullopt; }
    manifest.startMenu = root.value(QLatin1String("startMenu")).toBool(true);
    manifest.desktop = root.value(QLatin1String("desktop")).toBool(false);
    const QJsonValue shortcuts = root.value(QLatin1String("shortcutPaths"));
    if (!shortcuts.isArray() && !shortcuts.isUndefined()) { return std::nullopt; }
    for (const QJsonValue& item : shortcuts.toArray()) {
        if (!item.isString()) { return std::nullopt; }
        // Rejects the whole record, exactly like an unsafe entry in `files`:
        // every consumer of this list deletes what it names.
        if (!isSafeShortcutPath(item.toString())) { return std::nullopt; }
        manifest.shortcutPaths.append(item.toString());
    }
    manifest.installedUtc = root.value(QLatin1String("installedUtc")).toString();

    auto files = parseFiles(root.value(QLatin1String("files")), /*allowAlias=*/false);
    if (!files) { return std::nullopt; }
    manifest.files = std::move(*files);
    return manifest;
}

QByteArray InstalledManifest::toJson() const {
    QJsonObject root;
    root.insert(QLatin1String("schema"), schema);
    root.insert(QLatin1String("version"), version);
    root.insert(QLatin1String("installDir"), installDir);
    root.insert(QLatin1String("scope"), scope);
    root.insert(QLatin1String("startMenu"), startMenu);
    root.insert(QLatin1String("desktop"), desktop);
    root.insert(QLatin1String("shortcutPaths"), QJsonArray::fromStringList(shortcutPaths));
    root.insert(QLatin1String("installedUtc"), installedUtc);
    root.insert(QLatin1String("files"), filesToJson(files, /*withAlias=*/false));
    return toStableJson(root);
}

} // namespace dish::installer
