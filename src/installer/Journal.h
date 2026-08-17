// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The rollback journal: one JSON object per line, appended and flushed BEFORE
// the destructive step each entry covers, so a crash at any instant leaves a
// replayable record. Reverse-applying the entries restores the previous state
// (spec 3.5 ordering invariants, 11.2 recovery). The journal lives in the
// install dir (`.dish-journal.jsonl`) so the next installer/uninstaller run
// finds it; a half-written trailing line is tolerated and skipped on read.

#pragma once

#include "installer/ops/FileOps.h"
#include "installer/ops/RegistryOps.h"
#include "installer/ops/ShortcutOps.h"

#include <QByteArray>
#include <QFile>
#include <QString>
#include <QVector>

#include <optional>

namespace dish::installer {

// What the journaled step DID; the undo derives from it. Field meaning per
// action (all paths absolute unless said otherwise):
//   CreatedDir       path = dir                       undo: removeDirIfEmpty
//   CopiedFile       path = destination file          undo: remove
//   StagedOld        path = final file, aux = its
//                    `.dish-old` backup               undo: rename(aux, path)
//   PromotedStaged   path = final file, aux = its
//                    `.dish-stage` source             undo: rename(path, aux)
//   CreatedShortcut  path = resolved .lnk ("" until
//                    the executor resolves it), aux =
//                    "<location>|<scope>" tokens      undo: ShortcutOps::remove
//   WroteManifest    path = .dish-manifest.json       undo: remove
//   WroteArp         aux = scope token, prevArp =
//                    values to restore (nullopt on a
//                    fresh install)                   undo: writeArp / deleteArp
enum class JournalAction {
    CreatedDir,
    CopiedFile,
    StagedOld,
    PromotedStaged,
    CreatedShortcut,
    WroteManifest,
    WroteArp,
};

struct JournalEntry {
    JournalAction action = JournalAction::CopiedFile;
    QString path;
    QString aux;
    std::optional<ArpValues> prevArp;

    bool operator==(const JournalEntry& o) const {
        return action == o.action && path == o.path && aux == o.aux && prevArp == o.prevArp;
    }
    bool operator!=(const JournalEntry& o) const { return !(*this == o); }
};

inline QString journalFileName() { return QStringLiteral(".dish-journal.jsonl"); }
inline QString journalFilePath(const QString& installDir) {
    return installDir + QLatin1Char('/') + journalFileName();
}

// One compact JSON object terminated by '\n'; the exact inverse of
// journalEntryFromJsonLine. Unknown actions and malformed lines parse to
// nullopt so a tampered or torn journal degrades to "skip", never to a wrong
// undo.
QByteArray journalEntryToJsonLine(const JournalEntry& entry);
std::optional<JournalEntry> journalEntryFromJsonLine(const QByteArray& line);

// Append-only writer. Every append flushes to the OS before returning, which
// is what makes "journal before effect" worth anything.
class JournalWriter {
  public:
    JournalWriter() = default;
    ~JournalWriter();

    // Truncates: a journal only ever covers ONE attempt. The parent dir must
    // exist (the coordinator creates the install dir first).
    bool open(const QString& absPath);
    bool isOpen() const;
    QString path() const { return path_; }
    bool append(const JournalEntry& entry);
    void close();

  private:
    QFile file_;
    QString path_;
};

// Entries in file order (the order they were performed). nullopt when the file
// cannot be read at all; a readable file with only garbage lines yields an
// empty vector. A trailing half-written line is skipped silently.
std::optional<QVector<JournalEntry>> readJournal(const QString& absPath);

// Undoes ONE entry. Idempotent by design: an undo whose object is already gone
// (file/backup/shortcut/key missing) reports success, because replay must be
// safe over a journal that got half-rolled-back before a crash. A directory
// that is still non-empty also reports success — later cleanup retries it —
// so foreign files never flip a clean rollback to "incomplete" on their own.
bool applyJournalUndo(const JournalEntry& entry, FileOps& fileOps, RegistryOps& registryOps,
                      ShortcutOps& shortcutOps);

// The containment rule for an entry read back OFF DISK. A journal file sits in
// the install directory, and on a per-user install (or any directory the
// operator picks) that directory is writable by whoever can write there — while
// the recovery below turns every entry into a delete or a rename, and an
// AllUsers record makes UpdateApply run that recovery elevated. So a recovered
// entry may only name paths inside the install dir it was found in; a
// CreatedShortcut is admitted on its tokens alone (the link path is re-derived,
// never trusted) and WroteArp is bounded by its fixed registry key.
//
// Entries the coordinator replays from its own in-memory mirror during a live
// rollback do NOT go through here: it wrote them itself moments earlier, and its
// shortcut entries legitimately point outside the install dir.
bool journalEntryIsRecoverable(const JournalEntry& entry, const QString& installDir);

// The entry as it may be replayed: identical for everything except
// CreatedShortcut, whose resolved `path` is dropped so applyJournalUndo rebuilds
// it from the "<location>|<scope>" tokens instead of deleting whatever file the
// journal named.
JournalEntry sanitizedForRecovery(const JournalEntry& entry);

// The crashed-attempt sweep (spec 11.2): replays the on-disk journal in
// reverse best-effort, deletes the journal, then removes `.dish-old` /
// `.dish-stage` residue. Returns false when any undo or removal failed, or when
// an entry named a path outside `installDir` and was refused (the caller logs;
// the leftovers stay for the next attempt). A missing journal with residue
// present still cleans the residue.
bool recoverStaleJournal(const QString& installDir, FileOps& fileOps, RegistryOps& registryOps,
                         ShortcutOps& shortcutOps);

} // namespace dish::installer
