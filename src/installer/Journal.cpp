// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "installer/Journal.h"

#include "installer/InstallPlan.h"
#include "installer/ops/KnownFolders.h"
#include "installer/ops/Win32FileOps.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

namespace dish::installer {

namespace {

// Win32 codes the idempotent undos treat as "already done". Spelled as values
// so this TU does not need windows.h.
constexpr quint32 kErrorFileNotFound = 2;
constexpr quint32 kErrorPathNotFound = 3;
constexpr quint32 kErrorDirNotEmpty = 145;

bool alreadyGone(const OpResult& r) {
    return r.win32 == kErrorFileNotFound || r.win32 == kErrorPathNotFound;
}

// Forward slashes, `..` collapsed, case-folded: the form every containment
// comparison below uses. NTFS is case-insensitive and the journal is written
// with forward slashes, but a hand-crafted one need not be.
QString normalizedForCompare(const QString& path) {
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toCaseFolded();
}

bool isInside(const QString& path, const QString& root) {
    if (path.isEmpty() || root.isEmpty()) { return false; }
    const QString candidate = normalizedForCompare(path);
    const QString base = normalizedForCompare(root);
    if (base.isEmpty() || candidate.isEmpty()) { return false; }
    // The dir itself counts: CreatedDir journals the install dir on a fresh
    // install and its undo is the prune that removes it when it came out empty.
    return candidate == base || candidate.startsWith(base + QLatin1Char('/'));
}

const char* actionToken(JournalAction action) {
    switch (action) {
    case JournalAction::CreatedDir:
        return "createdDir";
    case JournalAction::CopiedFile:
        return "copiedFile";
    case JournalAction::StagedOld:
        return "stagedOld";
    case JournalAction::PromotedStaged:
        return "promotedStaged";
    case JournalAction::CreatedShortcut:
        return "createdShortcut";
    case JournalAction::WroteManifest:
        return "wroteManifest";
    case JournalAction::WroteArp:
        return "wroteArp";
    }
    return "copiedFile";
}

std::optional<JournalAction> actionFromToken(const QString& token) {
    if (token == QLatin1String("createdDir")) { return JournalAction::CreatedDir; }
    if (token == QLatin1String("copiedFile")) { return JournalAction::CopiedFile; }
    if (token == QLatin1String("stagedOld")) { return JournalAction::StagedOld; }
    if (token == QLatin1String("promotedStaged")) { return JournalAction::PromotedStaged; }
    if (token == QLatin1String("createdShortcut")) { return JournalAction::CreatedShortcut; }
    if (token == QLatin1String("wroteManifest")) { return JournalAction::WroteManifest; }
    if (token == QLatin1String("wroteArp")) { return JournalAction::WroteArp; }
    return std::nullopt;
}

QJsonObject arpToJson(const ArpValues& v) {
    QJsonObject obj;
    obj.insert(QLatin1String("displayName"), v.displayName);
    obj.insert(QLatin1String("displayVersion"), v.displayVersion);
    obj.insert(QLatin1String("versionMajor"), v.versionMajor);
    obj.insert(QLatin1String("versionMinor"), v.versionMinor);
    obj.insert(QLatin1String("publisher"), v.publisher);
    obj.insert(QLatin1String("displayIcon"), v.displayIcon);
    obj.insert(QLatin1String("installLocation"), v.installLocation);
    obj.insert(QLatin1String("installDate"), v.installDate);
    obj.insert(QLatin1String("uninstallString"), v.uninstallString);
    obj.insert(QLatin1String("quietUninstallString"), v.quietUninstallString);
    obj.insert(QLatin1String("estimatedSizeKiB"), static_cast<double>(v.estimatedSizeKiB));
    obj.insert(QLatin1String("urlInfoAbout"), v.urlInfoAbout);
    obj.insert(QLatin1String("helpLink"), v.helpLink);
    obj.insert(QLatin1String("installScope"), v.installScope);
    return obj;
}

ArpValues arpFromJson(const QJsonObject& obj) {
    ArpValues v;
    v.displayName = obj.value(QLatin1String("displayName")).toString();
    v.displayVersion = obj.value(QLatin1String("displayVersion")).toString();
    v.versionMajor = obj.value(QLatin1String("versionMajor")).toInt();
    v.versionMinor = obj.value(QLatin1String("versionMinor")).toInt();
    v.publisher = obj.value(QLatin1String("publisher")).toString();
    v.displayIcon = obj.value(QLatin1String("displayIcon")).toString();
    v.installLocation = obj.value(QLatin1String("installLocation")).toString();
    v.installDate = obj.value(QLatin1String("installDate")).toString();
    v.uninstallString = obj.value(QLatin1String("uninstallString")).toString();
    v.quietUninstallString = obj.value(QLatin1String("quietUninstallString")).toString();
    v.estimatedSizeKiB =
        static_cast<quint32>(obj.value(QLatin1String("estimatedSizeKiB")).toDouble());
    v.urlInfoAbout = obj.value(QLatin1String("urlInfoAbout")).toString();
    v.helpLink = obj.value(QLatin1String("helpLink")).toString();
    v.installScope = obj.value(QLatin1String("installScope")).toString();
    return v;
}

} // namespace

QByteArray journalEntryToJsonLine(const JournalEntry& entry) {
    QJsonObject obj;
    obj.insert(QLatin1String("action"), QLatin1String(actionToken(entry.action)));
    obj.insert(QLatin1String("path"), entry.path);
    if (!entry.aux.isEmpty()) { obj.insert(QLatin1String("aux"), entry.aux); }
    if (entry.prevArp) { obj.insert(QLatin1String("prevArp"), arpToJson(*entry.prevArp)); }
    QByteArray line = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

std::optional<JournalEntry> journalEntryFromJsonLine(const QByteArray& line) {
    QJsonParseError error{};
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) { return std::nullopt; }
    const QJsonObject obj = doc.object();
    const auto action = actionFromToken(obj.value(QLatin1String("action")).toString());
    if (!action) { return std::nullopt; }
    JournalEntry entry;
    entry.action = *action;
    entry.path = obj.value(QLatin1String("path")).toString();
    entry.aux = obj.value(QLatin1String("aux")).toString();
    if (obj.contains(QLatin1String("prevArp")) && obj.value(QLatin1String("prevArp")).isObject()) {
        entry.prevArp = arpFromJson(obj.value(QLatin1String("prevArp")).toObject());
    }
    return entry;
}

JournalWriter::~JournalWriter() { close(); }

bool JournalWriter::open(const QString& absPath) {
    close();
    file_.setFileName(absPath);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
    path_ = absPath;
    return true;
}

bool JournalWriter::isOpen() const { return file_.isOpen(); }

bool JournalWriter::append(const JournalEntry& entry) {
    if (!file_.isOpen()) { return false; }
    const QByteArray line = journalEntryToJsonLine(entry);
    if (file_.write(line) != line.size()) { return false; }
    return file_.flush();
}

void JournalWriter::close() {
    if (file_.isOpen()) { file_.close(); }
    path_.clear();
}

std::optional<QVector<JournalEntry>> readJournal(const QString& absPath) {
    QFile file(absPath);
    if (!file.open(QIODevice::ReadOnly)) { return std::nullopt; }
    QVector<JournalEntry> entries;
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) { continue; }
        if (const auto entry = journalEntryFromJsonLine(line)) { entries.append(*entry); }
        // Malformed lines (typically the torn tail of a crash) are skipped:
        // everything before them was flushed whole.
    }
    return entries;
}

bool applyJournalUndo(const JournalEntry& entry, FileOps& fileOps, RegistryOps& registryOps,
                      ShortcutOps& shortcutOps) {
    switch (entry.action) {
    case JournalAction::CreatedDir: {
        const OpResult r = fileOps.removeDirIfEmpty(entry.path);
        return r.ok || r.win32 == kErrorDirNotEmpty;
    }
    case JournalAction::CopiedFile:
    case JournalAction::WroteManifest: {
        const OpResult r = fileOps.remove(entry.path);
        return r.ok || alreadyGone(r);
    }
    case JournalAction::StagedOld: {
        if (!fileOps.exists(entry.aux)) { return true; } // nothing was staged
        const OpResult r = fileOps.rename(entry.aux, entry.path);
        return r.ok || alreadyGone(r);
    }
    case JournalAction::PromotedStaged: {
        if (!fileOps.exists(entry.path)) { return true; } // promote never ran
        const OpResult r = fileOps.rename(entry.path, entry.aux);
        return r.ok || alreadyGone(r);
    }
    case JournalAction::CreatedShortcut: {
        QString link = entry.path;
        if (link.isEmpty()) {
            // The entry predates resolution: rebuild the deterministic link
            // path from the "<location>|<scope>" tokens.
            const QStringList parts = entry.aux.split(QLatin1Char('|'));
            if (parts.size() != 2) { return true; }
            const auto location = shortcutLocationFromToken(parts.at(0));
            const auto scope = scopeFromToken(parts.at(1));
            if (!location || !scope) { return true; }
            link = shortcutLinkPath(*location, *scope);
            if (link.isEmpty()) { return true; }
        }
        const OpResult r = shortcutOps.remove(link);
        return r.ok || alreadyGone(r);
    }
    case JournalAction::WroteArp: {
        const auto scope = scopeFromToken(entry.aux);
        if (!scope) { return false; }
        if (entry.prevArp) { return registryOps.writeArp(*scope, *entry.prevArp).ok; }
        return registryOps.deleteArp(*scope).ok;
    }
    }
    return false;
}

bool journalEntryIsRecoverable(const JournalEntry& entry, const QString& installDir) {
    if (installDir.isEmpty()) { return false; }
    switch (entry.action) {
    case JournalAction::CreatedDir:
    case JournalAction::CopiedFile:
    case JournalAction::WroteManifest:
        return isInside(entry.path, installDir);
    case JournalAction::StagedOld:
    case JournalAction::PromotedStaged:
        // Both ends of the rename: the backup under `.dish-old` / the staged
        // copy under `.dish-stage`, and the final file. A rename is a WRITE to
        // its destination, so admitting only one end would still hand out an
        // arbitrary-file-write primitive.
        return isInside(entry.path, installDir) && isInside(entry.aux, installDir);
    case JournalAction::CreatedShortcut:
        // The link is rebuilt from the tokens by sanitizedForRecovery, so the
        // only reachable targets are the four known-folder Dish.lnk paths.
        return true;
    case JournalAction::WroteArp:
        // Both undos write or delete one fixed key under a hive the scope token
        // picks; nothing here names a path.
        return scopeFromToken(entry.aux).has_value();
    }
    return false;
}

JournalEntry sanitizedForRecovery(const JournalEntry& entry) {
    JournalEntry out = entry;
    if (out.action == JournalAction::CreatedShortcut) { out.path.clear(); }
    return out;
}

bool recoverStaleJournal(const QString& installDir, FileOps& fileOps, RegistryOps& registryOps,
                         ShortcutOps& shortcutOps) {
    bool allOk = true;
    const QString journalPath = journalFilePath(installDir);
    if (fileOps.exists(journalPath)) {
        const auto entries = readJournal(journalPath);
        if (entries) {
            for (int i = entries->size() - 1; i >= 0; --i) {
                const JournalEntry& entry = entries->at(i);
                if (!journalEntryIsRecoverable(entry, installDir)) {
                    // Refused, not undone: a journal naming something outside
                    // its own install dir is not a crashed attempt of ours.
                    allOk = false;
                    continue;
                }
                if (!applyJournalUndo(sanitizedForRecovery(entry), fileOps, registryOps,
                                      shortcutOps)) {
                    allOk = false;
                }
            }
        } else {
            allOk = false;
        }
        if (!fileOps.remove(journalPath).ok) { allOk = false; }
    }
    const QString stale[] = {installDir + QLatin1Char('/') + oldDirName(),
                             installDir + QLatin1Char('/') + stageDirName()};
    for (const QString& dir : stale) {
        if (!fileOps.exists(dir)) { continue; }
        if (!removeTreeBestEffort(fileOps, dir).ok) { allOk = false; }
    }
    return allOk;
}

} // namespace dish::installer
