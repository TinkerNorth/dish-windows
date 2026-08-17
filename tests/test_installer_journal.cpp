// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The rollback journal: line format, the torn-tail tolerance a crash leaves
// behind, the per-action undo, and the reverse-order replay the stale-journal
// sweep performs. The undos are asserted against the in-memory ops, so the
// "already gone" branches (which a real Win32FileOps hides by reporting
// success) are actually reachable.

#include "installer/Journal.h"

#include "installer/InstallPlan.h"
#include "installer/ops/RegistryOps.h"

#include "installer/FakeOps.h"

#include <catch2/catch_test_macros.hpp>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

using dish::installer::applyJournalUndo;
using dish::installer::ArpValues;
using dish::installer::JournalAction;
using dish::installer::JournalEntry;
using dish::installer::journalEntryFromJsonLine;
using dish::installer::journalEntryToJsonLine;
using dish::installer::journalFileName;
using dish::installer::journalFilePath;
using dish::installer::JournalWriter;
using dish::installer::readJournal;
using dish::installer::recoverStaleJournal;
using dish::installer::Scope;
using dish::test::FakeFileOps;
using dish::test::FakeRegistryOps;
using dish::test::FakeShortcutOps;

namespace {

JournalEntry make(JournalAction action, const QString& path, const QString& aux = QString()) {
    JournalEntry e;
    e.action = action;
    e.path = path;
    e.aux = aux;
    return e;
}

ArpValues arp(const QString& version) {
    ArpValues v;
    v.displayName = QStringLiteral("Dish");
    v.displayVersion = version;
    v.versionMajor = 0;
    v.versionMinor = 9;
    v.publisher = QStringLiteral("TinkerNorth");
    v.displayIcon = QStringLiteral("C:\\App\\dish.exe,0");
    v.installLocation = QStringLiteral("C:\\App");
    v.installDate = QStringLiteral("20260101");
    v.uninstallString = QStringLiteral("\"C:\\App\\uninstall.exe\"");
    v.quietUninstallString = QStringLiteral("\"C:\\App\\uninstall.exe\" --silent");
    v.estimatedSizeKiB = 42;
    v.urlInfoAbout = QStringLiteral("https://github.com/TinkerNorth/dish-windows");
    v.helpLink = v.urlInfoAbout;
    v.installScope = QStringLiteral("user");
    return v;
}

bool appendRaw(const QString& path, const QByteArray& bytes) {
    QFile file(path);
    if (!file.open(QIODevice::Append)) { return false; }
    return file.write(bytes) == bytes.size();
}

} // namespace

TEST_CASE("installer journal: the file name and location are part of the recovery contract",
          "[installer][journal]") {
    CHECK(journalFileName() == QStringLiteral(".dish-journal.jsonl"));
    CHECK(journalFilePath(QStringLiteral("C:/App")) ==
          QStringLiteral("C:/App/.dish-journal.jsonl"));
}

TEST_CASE("installer journal: every action round-trips through one JSON line",
          "[installer][journal]") {
    const QVector<JournalEntry> entries{
        make(JournalAction::CreatedDir, QStringLiteral("C:/App/licenses")),
        make(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe")),
        make(JournalAction::StagedOld, QStringLiteral("C:/App/dish.exe"),
             QStringLiteral("C:/App/.dish-old/dish.exe")),
        make(JournalAction::PromotedStaged, QStringLiteral("C:/App/dish.exe"),
             QStringLiteral("C:/App/.dish-stage/dish.exe")),
        make(JournalAction::CreatedShortcut, QString(), QStringLiteral("desktop|machine")),
        make(JournalAction::WroteManifest, QStringLiteral("C:/App/.dish-manifest.json")),
        make(JournalAction::WroteArp, QString(), QStringLiteral("user")),
    };

    for (const JournalEntry& entry : entries) {
        const QByteArray line = journalEntryToJsonLine(entry);
        CHECK(line.endsWith('\n'));
        CHECK(line.count('\n') == 1); // one object per line, compact
        const auto parsed = journalEntryFromJsonLine(line);
        REQUIRE(parsed.has_value());
        CHECK(*parsed == entry);
    }
}

TEST_CASE("installer journal: the previous ARP values survive the round trip",
          "[installer][journal]") {
    JournalEntry entry = make(JournalAction::WroteArp, QString(), QStringLiteral("machine"));
    entry.prevArp = arp(QStringLiteral("0.9.0"));
    const auto parsed = journalEntryFromJsonLine(journalEntryToJsonLine(entry));
    REQUIRE(parsed.has_value());
    CHECK(*parsed == entry);
    REQUIRE(parsed->prevArp.has_value());
    CHECK(parsed->prevArp->displayVersion == QStringLiteral("0.9.0"));
    CHECK(parsed->prevArp->estimatedSizeKiB == 42u);
}

TEST_CASE("installer journal: a tampered or torn line parses to nothing, never to a wrong undo",
          "[installer][journal]") {
    CHECK_FALSE(
        journalEntryFromJsonLine(QByteArray("{\"action\":\"nukeEverything\"}")).has_value());
    CHECK_FALSE(journalEntryFromJsonLine(QByteArray("{\"path\":\"C:/App\"}")).has_value());
    CHECK_FALSE(journalEntryFromJsonLine(QByteArray("{\"action\":\"copiedF")).has_value());
    CHECK_FALSE(journalEntryFromJsonLine(QByteArray("not json at all")).has_value());
    CHECK_FALSE(journalEntryFromJsonLine(QByteArray()).has_value());
    CHECK_FALSE(journalEntryFromJsonLine(QByteArray("[1,2,3]")).has_value());
}

TEST_CASE("installer journal: the writer flushes each line and truncates on open",
          "[installer][journal]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString path = QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/j.jsonl");

    {
        JournalWriter writer;
        REQUIRE(writer.open(path));
        CHECK(writer.isOpen());
        CHECK(writer.path() == path);
        REQUIRE(writer.append(make(JournalAction::CreatedDir, QStringLiteral("C:/App"))));
        REQUIRE(writer.append(make(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe"))));
        // Flushed per append: readable while the writer is still open, which is
        // what makes "journal before effect" worth anything after a crash.
        const auto midRun = readJournal(path);
        REQUIRE(midRun.has_value());
        CHECK(midRun->size() == 2);
    }

    const auto entries = readJournal(path);
    REQUIRE(entries.has_value());
    REQUIRE(entries->size() == 2);
    CHECK(entries->at(0).action == JournalAction::CreatedDir);
    CHECK(entries->at(1).path == QStringLiteral("C:/App/dish.exe"));

    // A journal only ever covers ONE attempt.
    JournalWriter second;
    REQUIRE(second.open(path));
    second.close();
    const auto truncated = readJournal(path);
    REQUIRE(truncated.has_value());
    CHECK(truncated->isEmpty());
}

TEST_CASE("installer journal: a half-written trailing line is skipped, the rest is kept",
          "[installer][journal]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString path = QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/j.jsonl");
    {
        JournalWriter writer;
        REQUIRE(writer.open(path));
        REQUIRE(writer.append(make(JournalAction::CreatedDir, QStringLiteral("C:/App"))));
        REQUIRE(writer.append(make(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe"))));
    }
    // The tail a power loss leaves: a partial object with no newline.
    REQUIRE(appendRaw(path, "{\"action\":\"copiedFile\",\"path\":\"C:/App/qt6core.d"));

    const auto entries = readJournal(path);
    REQUIRE(entries.has_value());
    CHECK(entries->size() == 2);
    CHECK(entries->at(1).path == QStringLiteral("C:/App/dish.exe"));
}

TEST_CASE("installer journal: an unreadable journal is nullopt, not an empty replay",
          "[installer][journal]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    CHECK_FALSE(readJournal(QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/none.jsonl"))
                    .has_value());
}

TEST_CASE("installer journal: undo of a created directory", "[installer][journal]") {
    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    files.addDir(QStringLiteral("C:/App"));

    CHECK(applyJournalUndo(make(JournalAction::CreatedDir, QStringLiteral("C:/App")), files,
                           registry, shortcuts));
    CHECK_FALSE(files.hasDir(QStringLiteral("C:/App")));

    // A directory that is not empty (a foreign file survived) is a SOFT
    // success: cleanup retries it, and it must never flip a clean rollback to
    // "incomplete" on its own.
    files.addDir(QStringLiteral("C:/Other"));
    files.addFile(QStringLiteral("C:/Other/foreign.txt"), 1);
    CHECK(applyJournalUndo(make(JournalAction::CreatedDir, QStringLiteral("C:/Other")), files,
                           registry, shortcuts));
    CHECK(files.hasDir(QStringLiteral("C:/Other")));

    // Already gone.
    CHECK(applyJournalUndo(make(JournalAction::CreatedDir, QStringLiteral("C:/Gone")), files,
                           registry, shortcuts));
}

TEST_CASE("installer journal: undo of a copied file and of the installed manifest",
          "[installer][journal]") {
    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    files.addFile(QStringLiteral("C:/App/dish.exe"), 10);
    files.addFile(QStringLiteral("C:/App/.dish-manifest.json"), 2);

    CHECK(applyJournalUndo(make(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe")),
                           files, registry, shortcuts));
    CHECK_FALSE(files.hasFile(QStringLiteral("C:/App/dish.exe")));

    CHECK(applyJournalUndo(
        make(JournalAction::WroteManifest, QStringLiteral("C:/App/.dish-manifest.json")), files,
        registry, shortcuts));
    CHECK_FALSE(files.hasFile(QStringLiteral("C:/App/.dish-manifest.json")));

    // Idempotent: replaying a half-rolled-back journal must be safe.
    CHECK(applyJournalUndo(make(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe")),
                           files, registry, shortcuts));

    // A locked file is a real failure.
    files.addFile(QStringLiteral("C:/App/locked.dll"), 3);
    files.failOn(QStringLiteral("C:/App/locked.dll"));
    CHECK_FALSE(
        applyJournalUndo(make(JournalAction::CopiedFile, QStringLiteral("C:/App/locked.dll")),
                         files, registry, shortcuts));
}

TEST_CASE("installer journal: undo of the upgrade renames restores the previous file",
          "[installer][journal]") {
    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    // StagedOld: the old file was moved aside, so the undo moves it back.
    files.addFile(QStringLiteral("C:/App/.dish-old/dish.exe"), 10);
    CHECK(applyJournalUndo(make(JournalAction::StagedOld, QStringLiteral("C:/App/dish.exe"),
                                QStringLiteral("C:/App/.dish-old/dish.exe")),
                           files, registry, shortcuts));
    CHECK(files.hasFile(QStringLiteral("C:/App/dish.exe")));
    CHECK_FALSE(files.hasFile(QStringLiteral("C:/App/.dish-old/dish.exe")));

    // Nothing was staged: the step never ran, so the undo is a no-op success.
    FakeFileOps empty;
    CHECK(applyJournalUndo(make(JournalAction::StagedOld, QStringLiteral("C:/App/dish.exe"),
                                QStringLiteral("C:/App/.dish-old/dish.exe")),
                           empty, registry, shortcuts));
    CHECK(empty.calls().isEmpty()); // it only probed exists(); no rename was attempted
    CHECK_FALSE(empty.hasFile(QStringLiteral("C:/App/dish.exe")));

    // PromotedStaged: the new file goes back to the stage dir.
    FakeFileOps promoted;
    promoted.addFile(QStringLiteral("C:/App/dish.exe"), 11);
    CHECK(applyJournalUndo(make(JournalAction::PromotedStaged, QStringLiteral("C:/App/dish.exe"),
                                QStringLiteral("C:/App/.dish-stage/dish.exe")),
                           promoted, registry, shortcuts));
    CHECK(promoted.hasFile(QStringLiteral("C:/App/.dish-stage/dish.exe")));
    CHECK_FALSE(promoted.hasFile(QStringLiteral("C:/App/dish.exe")));

    // The promote never ran.
    FakeFileOps notPromoted;
    CHECK(applyJournalUndo(make(JournalAction::PromotedStaged, QStringLiteral("C:/App/dish.exe"),
                                QStringLiteral("C:/App/.dish-stage/dish.exe")),
                           notPromoted, registry, shortcuts));
}

TEST_CASE("installer journal: undo of a shortcut, resolved and unresolved",
          "[installer][journal]") {
    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;

    dish::installer::ShortcutSpec spec;
    spec.location = dish::installer::ShortcutLocation::Desktop;
    spec.scope = Scope::PerUser;
    const auto created = shortcuts.create(spec);
    REQUIRE(created.ok);

    CHECK(applyJournalUndo(make(JournalAction::CreatedShortcut, created.path), files, registry,
                           shortcuts));
    CHECK_FALSE(shortcuts.exists(created.path));

    // Already gone is success (idempotent replay).
    CHECK(applyJournalUndo(make(JournalAction::CreatedShortcut, created.path), files, registry,
                           shortcuts));

    // An entry written BEFORE the executor resolved the path rebuilds the
    // deterministic link location from its "<location>|<scope>" tokens.
    const int callsBefore = shortcuts.calls().size();
    CHECK(applyJournalUndo(
        make(JournalAction::CreatedShortcut, QString(), QStringLiteral("startmenu|user")), files,
        registry, shortcuts));
    REQUIRE(shortcuts.calls().size() == callsBefore + 1);
    CHECK(shortcuts.calls().last().endsWith(QStringLiteral("Dish.lnk")));

    // Unparsable tokens undo nothing rather than guessing at a path.
    const int callsAfter = shortcuts.calls().size();
    CHECK(applyJournalUndo(make(JournalAction::CreatedShortcut, QString(), QStringLiteral("junk")),
                           files, registry, shortcuts));
    CHECK(shortcuts.calls().size() == callsAfter);
}

TEST_CASE("installer journal: undo of the ARP write deletes or restores", "[installer][journal]") {
    FakeFileOps files;
    FakeShortcutOps shortcuts;

    SECTION("a fresh install had no entry: the undo deletes it") {
        FakeRegistryOps registry;
        registry.seed(Scope::PerUser, arp(QStringLiteral("1.0.0")));
        CHECK(applyJournalUndo(make(JournalAction::WroteArp, QString(), QStringLiteral("user")),
                               files, registry, shortcuts));
        CHECK_FALSE(registry.stored(Scope::PerUser).has_value());
        CHECK(registry.calls().contains(QStringLiteral("deleteArp user")));
    }
    SECTION("an upgrade restores the previous values verbatim") {
        FakeRegistryOps registry;
        registry.seed(Scope::AllUsers, arp(QStringLiteral("2.0.0")));
        JournalEntry entry = make(JournalAction::WroteArp, QString(), QStringLiteral("machine"));
        entry.prevArp = arp(QStringLiteral("0.9.0"));
        CHECK(applyJournalUndo(entry, files, registry, shortcuts));
        REQUIRE(registry.stored(Scope::AllUsers).has_value());
        CHECK(registry.stored(Scope::AllUsers)->displayVersion == QStringLiteral("0.9.0"));
    }
    SECTION("an unknown scope token is a failure, not a guess") {
        FakeRegistryOps registry;
        CHECK_FALSE(
            applyJournalUndo(make(JournalAction::WroteArp, QString(), QStringLiteral("everyone")),
                             files, registry, shortcuts));
    }
    SECTION("a registry that refuses the delete reports incomplete") {
        FakeRegistryOps registry;
        registry.failDeletes(true);
        CHECK_FALSE(
            applyJournalUndo(make(JournalAction::WroteArp, QString(), QStringLiteral("user")),
                             files, registry, shortcuts));
    }
}

TEST_CASE("installer journal: the stale-journal sweep replays in reverse and clears the residue",
          "[installer][journal]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString installDir = QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/App");
    REQUIRE(QDir().mkpath(installDir));
    const QString path = journalFilePath(installDir);

    {
        JournalWriter writer;
        REQUIRE(writer.open(path));
        REQUIRE(writer.append(make(JournalAction::CreatedDir, installDir)));
        REQUIRE(
            writer.append(make(JournalAction::CopiedFile, installDir + QStringLiteral("/a.dll"))));
        REQUIRE(
            writer.append(make(JournalAction::CopiedFile, installDir + QStringLiteral("/b.dll"))));
    }

    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    files.addDir(installDir);
    files.addFile(path, 1);
    files.addFile(installDir + QStringLiteral("/a.dll"), 1);
    files.addFile(installDir + QStringLiteral("/b.dll"), 1);
    files.addDir(installDir + QStringLiteral("/.dish-old"));
    files.addFile(installDir + QStringLiteral("/.dish-old/dish.exe"), 1);
    files.addDir(installDir + QStringLiteral("/.dish-stage"));

    CHECK(recoverStaleJournal(installDir, files, registry, shortcuts));

    // Reverse entry order: b.dll, a.dll, then the directory.
    const QStringList calls = files.calls();
    const int b = calls.indexOf(QStringLiteral("remove ") + installDir + QStringLiteral("/b.dll"));
    const int a = calls.indexOf(QStringLiteral("remove ") + installDir + QStringLiteral("/a.dll"));
    const int dir = calls.indexOf(QStringLiteral("removeDirIfEmpty ") + installDir);
    REQUIRE(b >= 0);
    REQUIRE(a >= 0);
    REQUIRE(dir >= 0);
    CHECK(b < a);
    CHECK(a < dir);

    CHECK_FALSE(files.hasFile(path)); // the journal is gone
    CHECK_FALSE(files.hasFile(installDir + QStringLiteral("/.dish-old/dish.exe")));
    CHECK_FALSE(files.hasDir(installDir + QStringLiteral("/.dish-old")));
    CHECK_FALSE(files.hasDir(installDir + QStringLiteral("/.dish-stage")));
}

// ── A journal read back off disk is untrusted input ─────────────────────────
// The file lives in the install dir, so whoever can write there writes it, and
// every entry it carries becomes a delete or a rename. UpdateApply reaches this
// sweep with an install dir taken straight from --target-exe, and runs it
// ELEVATED when the record beside that exe says scope "machine" — so an
// unconstrained replay is an arbitrary-file-delete and arbitrary-file-write
// primitive, not just a wrong rollback.

TEST_CASE("installer journal: a recovered entry may only name paths inside the install dir",
          "[installer][journal]") {
    using dish::installer::journalEntryIsRecoverable;
    const QString installDir = QStringLiteral("C:/Program Files/Dish");

    CHECK(journalEntryIsRecoverable(make(JournalAction::CreatedDir, installDir), installDir));
    CHECK(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, installDir + QStringLiteral("/a/b.dll")), installDir));
    CHECK(journalEntryIsRecoverable(
        make(JournalAction::WroteManifest, installDir + QStringLiteral("/.dish-manifest.json")),
        installDir));

    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, QStringLiteral("C:/Windows/System32/drivers/etc/hosts")),
        installDir));
    // Backslashes and "." / ".." are normalized before the compare, so neither
    // spelling smuggles a path back out.
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, QStringLiteral("C:\\Windows\\notepad.exe")), installDir));
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, installDir + QStringLiteral("/../../Windows/notepad.exe")),
        installDir));
    CHECK(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, installDir + QStringLiteral("/sub/../dish.exe")),
        installDir));
    // A sibling whose name merely starts the same way is outside.
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::CopiedFile, QStringLiteral("C:/Program Files/Dish2/x.dll")),
        installDir));
    CHECK_FALSE(journalEntryIsRecoverable(make(JournalAction::CopiedFile, QString()), installDir));

    // A rename is a write to its destination, so BOTH ends have to be inside.
    CHECK(journalEntryIsRecoverable(make(JournalAction::StagedOld,
                                         installDir + QStringLiteral("/dish.exe"),
                                         installDir + QStringLiteral("/.dish-old/dish.exe")),
                                    installDir));
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::StagedOld, QStringLiteral("C:/Windows/System32/evil.dll"),
             installDir + QStringLiteral("/.dish-old/payload.dll")),
        installDir));
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::PromotedStaged, installDir + QStringLiteral("/dish.exe"),
             QStringLiteral("C:/Users/victim/Documents/thesis.docx")),
        installDir));

    // The ARP undo names no path; the scope token is the only thing to check.
    CHECK(journalEntryIsRecoverable(
        make(JournalAction::WroteArp, QString(), QStringLiteral("user")), installDir));
    CHECK_FALSE(journalEntryIsRecoverable(
        make(JournalAction::WroteArp, QString(), QStringLiteral("everyone")), installDir));
}

TEST_CASE("installer journal: a recovered shortcut undo is rebuilt from its tokens",
          "[installer][journal]") {
    using dish::installer::sanitizedForRecovery;
    // The recorded link path is dropped, so the only reachable targets are the
    // four deterministic known-folder Dish.lnk paths applyJournalUndo derives.
    const JournalEntry hostile = make(JournalAction::CreatedShortcut,
                                      QStringLiteral("C:/Users/victim/Documents/thesis.docx"),
                                      QStringLiteral("desktop|user"));
    const JournalEntry sanitized = sanitizedForRecovery(hostile);
    CHECK(sanitized.path.isEmpty());
    CHECK(sanitized.aux == QStringLiteral("desktop|user"));

    // Nothing else is touched.
    const JournalEntry file = make(JournalAction::CopiedFile, QStringLiteral("C:/App/a.dll"));
    CHECK(sanitizedForRecovery(file) == file);
}

TEST_CASE("installer journal: the stale sweep refuses an entry that escapes the install dir",
          "[installer][journal]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const QString installDir = QDir::fromNativeSeparators(temp.path()) + QStringLiteral("/App");
    REQUIRE(QDir().mkpath(installDir));
    const QString path = journalFilePath(installDir);
    const QString outside = QStringLiteral("C:/Windows/System32/victim.dll");
    const QString planted = QStringLiteral("C:/Users/attacker/payload.dll");

    {
        JournalWriter writer;
        REQUIRE(writer.open(path));
        // Delete anything...
        REQUIRE(writer.append(make(JournalAction::CopiedFile, outside)));
        // ...and write anywhere, by "restoring" a planted file over it.
        REQUIRE(writer.append(make(JournalAction::StagedOld, outside, planted)));
        // One legitimate entry so the sweep is proven to still do its job.
        REQUIRE(
            writer.append(make(JournalAction::CopiedFile, installDir + QStringLiteral("/a.dll"))));
    }

    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    files.addDir(installDir);
    files.addFile(path, 1);
    files.addFile(installDir + QStringLiteral("/a.dll"), 1);
    files.addFile(outside, 1);
    files.addFile(planted, 1);

    // What is at stake, spelled out: applyJournalUndo itself is deliberately
    // unconstrained (the live rollback replays entries the coordinator wrote
    // moments earlier, and its shortcut entries legitimately sit outside the
    // install dir), so these two entries ARE a delete-anything and a
    // write-anything primitive the moment they are replayed unchecked.
    {
        FakeFileOps loose;
        loose.addFile(outside, 1);
        loose.addFile(planted, 1);
        CHECK(
            applyJournalUndo(make(JournalAction::CopiedFile, outside), loose, registry, shortcuts));
        CHECK_FALSE(loose.hasFile(outside));
        CHECK(applyJournalUndo(make(JournalAction::StagedOld, outside, planted), loose, registry,
                               shortcuts));
        CHECK(loose.hasFile(outside)); // the planted bytes, moved into place
        CHECK_FALSE(loose.hasFile(planted));
    }

    // Refused entries are residue, not silent successes.
    CHECK_FALSE(recoverStaleJournal(installDir, files, registry, shortcuts));

    CHECK(files.hasFile(outside)); // never removed
    CHECK(files.hasFile(planted)); // never renamed over anything
    CHECK_FALSE(files.hasFile(installDir + QStringLiteral("/a.dll"))); // the real undo still ran
    CHECK_FALSE(files.hasFile(path));                                  // and the journal is gone
    for (const QString& call : files.calls()) {
        CHECK_FALSE(call.contains(outside));
        CHECK_FALSE(call.contains(planted));
    }
}

TEST_CASE("installer journal: residue is cleaned even with no journal left behind",
          "[installer][journal]") {
    FakeFileOps files;
    FakeRegistryOps registry;
    FakeShortcutOps shortcuts;
    const QString installDir = QStringLiteral("C:/App");
    files.addDir(installDir);
    files.addDir(installDir + QStringLiteral("/.dish-stage"));
    files.addFile(installDir + QStringLiteral("/.dish-stage/dish.exe"), 5);

    CHECK(recoverStaleJournal(installDir, files, registry, shortcuts));
    CHECK_FALSE(files.hasDir(installDir + QStringLiteral("/.dish-stage")));
    CHECK(files.hasDir(installDir)); // the install dir itself is never swept away
}
