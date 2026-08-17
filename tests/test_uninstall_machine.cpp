// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The uninstall reducer, phase x event. Uninstall is forward-only: there is no
// rollback, so what the tests pin is ORDER (shortcuts, files, residue, updates
// cache, purge, prune, helper handoff, finish), the purge gate, and the rule
// that the engine only ever names files the manifest recorded — the helper,
// spawned last, is what deletes the ARP key after this process exits.

#include "installer/UninstallMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <QVector>

#include <variant>
#include <vector>

using dish::installer::ExitCode;
using dish::installer::InstalledManifest;
using dish::installer::PayloadEntry;
using dish::installer::ProcInfo;
using dish::installer::reduce;
using dish::installer::SetupError;
using dish::installer::UninstallEffect;
using dish::installer::UninstallEvent;
using dish::installer::UninstallPhase;
using dish::installer::UninstallState;
namespace uev = dish::installer::uevent;
namespace ufx = dish::installer::ueffect;
namespace tag = dish::installer::effecttag;

namespace {

PayloadEntry entry(const QString& path) {
    PayloadEntry e;
    e.path = path;
    e.stagedAs = path;
    e.size = 1;
    e.sha256Hex = QByteArray(64, 'b');
    return e;
}

InstalledManifest recorded(bool desktopShortcut = true) {
    InstalledManifest m;
    m.version = QStringLiteral("1.2.3");
    m.installDir = QStringLiteral("C:/App");
    m.scope = QStringLiteral("user");
    m.startMenu = true;
    m.desktop = desktopShortcut;
    m.shortcutPaths = QStringList{QStringLiteral("C:/Menu/Dish.lnk")};
    if (desktopShortcut) { m.shortcutPaths.append(QStringLiteral("C:/Desktop/Dish.lnk")); }
    m.installedUtc = QStringLiteral("2026-08-04T10:00:00Z");
    m.files = QVector<PayloadEntry>{entry(QStringLiteral("dish.exe")),
                                    entry(QStringLiteral("licenses/LICENSE.txt"))};
    return m;
}

UninstallState state(UninstallPhase phase, bool purge = false) {
    UninstallState s;
    s.phase = phase;
    s.installDir = QStringLiteral("C:/App");
    s.scope = QStringLiteral("user");
    s.purgeUserData = purge;
    s.fileCount = 2;
    return s;
}

template <class T> int indexOf(const std::vector<UninstallEffect>& effects) {
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (std::holds_alternative<T>(effects.at(i))) { return static_cast<int>(i); }
    }
    return -1;
}

} // namespace

TEST_CASE("installer uninstall machine: Begin emits the removal pipeline in order",
          "[installer][uninstall-fsm]") {
    const InstalledManifest manifest = recorded();
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{manifest, false});

    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == UninstallPhase::Preflight);
    CHECK(r.next->installDir == QStringLiteral("C:/App"));
    CHECK(r.next->scope == QStringLiteral("user"));
    CHECK_FALSE(r.next->purgeUserData);
    CHECK(r.next->fileCount == 2);

    const std::vector<UninstallEffect> expected{
        ufx::ScanProcesses{QStringLiteral("C:/App")},
        ufx::RemoveShortcut{QStringLiteral("C:/Menu/Dish.lnk")},
        ufx::RemoveShortcut{QStringLiteral("C:/Desktop/Dish.lnk")},
        ufx::RemoveFile{QStringLiteral("C:/App/dish.exe")},
        ufx::RemoveFile{QStringLiteral("C:/App/licenses/LICENSE.txt")},
        ufx::RemoveResidue{QStringLiteral("C:/App")},
        // Unconditional: the updater cache is not user data (spec D13).
        ufx::RemoveUpdatesCache{},
        ufx::PruneDirs{QStringLiteral("C:/App")},
        // Empty fields: the executor resolves the temp helper copy, the
        // leftover list and its own pid.
        ufx::SpawnHelper{QString(), QStringList()},
        ufx::Finish{ExitCode::Ok},
    };
    CHECK(r.effects == expected);
}

TEST_CASE("installer uninstall machine: purge is emitted only when requested, and after the files",
          "[installer][uninstall-fsm]") {
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{recorded(), true});
    REQUIRE(r.next.has_value());
    CHECK(r.next->purgeUserData);

    const int purge = indexOf<ufx::PurgeUserData>(r.effects);
    REQUIRE(purge >= 0);
    CHECK(purge > indexOf<ufx::RemoveFile>(r.effects));
    CHECK(purge > indexOf<ufx::RemoveUpdatesCache>(r.effects));
    CHECK(purge < indexOf<ufx::PruneDirs>(r.effects));
}

TEST_CASE("installer uninstall machine: the helper is spawned last, before Finish",
          "[installer][uninstall-fsm]") {
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{recorded(), true});
    const int spawn = indexOf<ufx::SpawnHelper>(r.effects);
    const int finish = indexOf<ufx::Finish>(r.effects);
    REQUIRE(spawn >= 0);
    CHECK(finish == spawn + 1);
    CHECK(finish == static_cast<int>(r.effects.size()) - 1);
}

TEST_CASE("installer uninstall machine: shortcuts are removed before any file",
          "[installer][uninstall-fsm]") {
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{recorded(), false});
    CHECK(indexOf<ufx::RemoveShortcut>(r.effects) < indexOf<ufx::RemoveFile>(r.effects));
    CHECK(indexOf<ufx::RemoveFile>(r.effects) < indexOf<ufx::RemoveResidue>(r.effects));
}

TEST_CASE("installer uninstall machine: only manifest-recorded files are named",
          "[installer][uninstall-fsm]") {
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{recorded(), false});
    QStringList removed;
    for (const UninstallEffect& effect : r.effects) {
        if (const auto* file = std::get_if<ufx::RemoveFile>(&effect)) { removed.append(file->abs); }
    }
    // Exactly the two recorded entries: a foreign file in the install dir is
    // never in this list, which is what makes the engine incapable of deleting
    // something it did not put there.
    CHECK(removed == QStringList{QStringLiteral("C:/App/dish.exe"),
                                 QStringLiteral("C:/App/licenses/LICENSE.txt")});
}

TEST_CASE("installer uninstall machine: a record with no shortcuts emits none",
          "[installer][uninstall-fsm]") {
    InstalledManifest manifest = recorded();
    manifest.shortcutPaths.clear();
    const auto r = reduce(state(UninstallPhase::Idle), uev::Begin{manifest, false});
    CHECK(indexOf<ufx::RemoveShortcut>(r.effects) == -1);
}

TEST_CASE("installer uninstall machine: Preflight outcomes", "[installer][uninstall-fsm]") {
    SECTION("clear") {
        const auto r = reduce(state(UninstallPhase::Preflight), uev::BlockersGone{});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::RemovingShortcuts);
        CHECK(r.effects.empty());
    }
    SECTION("blocked") {
        const QVector<ProcInfo> procs{
            ProcInfo{11, QStringLiteral("C:/App/dish.exe"), QStringLiteral("dish.exe")}};
        const auto r = reduce(state(UninstallPhase::Preflight), uev::BlockersFound{procs});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::AwaitingBlockers);
        // The close policy is injected by the coordinator as CloseAppsRequested.
        CHECK(r.effects.empty());
    }
    SECTION("nothing installed") {
        const auto r = reduce(state(UninstallPhase::Preflight),
                              uev::PreflightFail{SetupError::NothingInstalled});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::Failed);
        CHECK(r.next->error == SetupError::NothingInstalled);
        const std::vector<UninstallEffect> expected{ufx::Finish{ExitCode::NothingInstalled}};
        CHECK(r.effects == expected);
    }
    SECTION("cancelled before anything was removed") {
        const auto r = reduce(state(UninstallPhase::Preflight), uev::CancelRequested{});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::Failed);
        CHECK(r.next->error == SetupError::Cancelled);
        const std::vector<UninstallEffect> expected{ufx::Finish{ExitCode::Cancelled}};
        CHECK(r.effects == expected);
    }
    SECTION("a preflight effect failure stops the run") {
        const auto r = reduce(state(UninstallPhase::Preflight),
                              uev::EffectFail{tag::scanProcesses, SetupError::AppRunning,
                                              QStringLiteral("C:/App/dish.exe")});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::Failed);
        const std::vector<UninstallEffect> expected{ufx::Finish{ExitCode::AppRunning}};
        CHECK(r.effects == expected);
    }
    SECTION("PreflightOk is a pass-through") {
        const auto r = reduce(state(UninstallPhase::Preflight), uev::PreflightOk{});
        CHECK_FALSE(r.next.has_value());
        CHECK(r.effects.empty());
    }
}

TEST_CASE("installer uninstall machine: AwaitingBlockers resolves through the user's choice",
          "[installer][uninstall-fsm]") {
    SECTION("close") {
        const auto r =
            reduce(state(UninstallPhase::AwaitingBlockers), uev::CloseAppsRequested{false});
        CHECK_FALSE(r.next.has_value());
        const std::vector<UninstallEffect> expected{
            ufx::CloseProcesses{QVector<ProcInfo>(), false, 10000}};
        CHECK(r.effects == expected);
    }
    SECTION("force close") {
        const auto r =
            reduce(state(UninstallPhase::AwaitingBlockers), uev::CloseAppsRequested{true});
        const std::vector<UninstallEffect> expected{
            ufx::CloseProcesses{QVector<ProcInfo>(), true, 10000}};
        CHECK(r.effects == expected);
    }
    SECTION("a survived close does not re-close automatically") {
        const QVector<ProcInfo> procs{
            ProcInfo{11, QStringLiteral("C:/App/dish.exe"), QStringLiteral("dish.exe")}};
        const auto r = reduce(state(UninstallPhase::AwaitingBlockers), uev::BlockersFound{procs});
        CHECK_FALSE(r.next.has_value());
        CHECK(r.effects.empty());
    }
    SECTION("gone") {
        const auto r = reduce(state(UninstallPhase::AwaitingBlockers), uev::BlockersGone{});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::RemovingShortcuts);
    }
    SECTION("cancel") {
        const auto r = reduce(state(UninstallPhase::AwaitingBlockers), uev::CancelRequested{});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == UninstallPhase::Failed);
        CHECK(r.next->error == SetupError::Cancelled);
    }
}

TEST_CASE("installer uninstall machine: acknowledgements walk the phases forward",
          "[installer][uninstall-fsm]") {
    const UninstallState removing = state(UninstallPhase::RemovingShortcuts);

    // A shortcut ack in RemovingShortcuts changes nothing: no re-emit.
    CHECK_FALSE(reduce(removing, uev::EffectOk{tag::removeShortcut, 0}).next.has_value());

    const auto file = reduce(removing, uev::EffectOk{tag::removeFile, 1});
    REQUIRE(file.next.has_value());
    CHECK(file.next->phase == UninstallPhase::RemovingFiles);
    CHECK(file.next->fileIndex == 2); // 1-based progress

    const auto residue = reduce(removing, uev::EffectOk{tag::removeResidue, 0});
    REQUIRE(residue.next.has_value());
    CHECK(residue.next->phase == UninstallPhase::RemovingFiles);

    const auto cache = reduce(removing, uev::EffectOk{tag::removeUpdatesCache, 0});
    REQUIRE(cache.next.has_value());
    CHECK(cache.next->phase == UninstallPhase::RemovingFiles);

    const auto purge = reduce(removing, uev::EffectOk{tag::purgeUserData, 0});
    REQUIRE(purge.next.has_value());
    CHECK(purge.next->phase == UninstallPhase::PurgingData);

    const auto handoff = reduce(removing, uev::EffectOk{tag::spawnHelper, 0});
    REQUIRE(handoff.next.has_value());
    CHECK(handoff.next->phase == UninstallPhase::HandingOff);

    const auto done = reduce(removing, uev::EffectOk{tag::finish, 0});
    REQUIRE(done.next.has_value());
    CHECK(done.next->phase == UninstallPhase::Done);
}

TEST_CASE("installer uninstall machine: cancel is ignored once removal started",
          "[installer][uninstall-fsm]") {
    for (const UninstallPhase phase :
         {UninstallPhase::RemovingShortcuts, UninstallPhase::RemovingFiles,
          UninstallPhase::PurgingData, UninstallPhase::HandingOff}) {
        const auto r = reduce(state(phase), uev::CancelRequested{});
        CHECK_FALSE(r.next.has_value());
        CHECK(r.effects.empty());
    }
}

TEST_CASE("installer uninstall machine: an unrecoverable removal failure reports and stops",
          "[installer][uninstall-fsm]") {
    // File-level trouble never reaches the reducer (the executor defers locked
    // files to the helper); what does is forward damage with no undo.
    const auto r = reduce(state(UninstallPhase::HandingOff),
                          uev::EffectFail{tag::spawnHelper, SetupError::Internal,
                                          QStringLiteral("C:/Temp/uninstall-helper.exe")});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == UninstallPhase::Failed);
    CHECK(r.next->errorPath == QStringLiteral("C:/Temp/uninstall-helper.exe"));
    const std::vector<UninstallEffect> expected{ufx::Finish{ExitCode::Internal}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer uninstall machine: Done and Failed are terminal",
          "[installer][uninstall-fsm]") {
    for (const UninstallPhase phase : {UninstallPhase::Done, UninstallPhase::Failed}) {
        for (const UninstallEvent& event :
             {UninstallEvent{uev::Begin{recorded(), false}}, UninstallEvent{uev::BlockersGone{}},
              UninstallEvent{uev::CancelRequested{}},
              UninstallEvent{uev::EffectOk{tag::finish, 0}}}) {
            const auto r = reduce(state(phase), event);
            CHECK_FALSE(r.next.has_value());
            CHECK(r.effects.empty());
        }
    }
}

TEST_CASE("installer uninstall machine: reduce is total over every phase and event",
          "[installer][uninstall-fsm]") {
    const std::vector<UninstallPhase> phases{UninstallPhase::Idle,
                                             UninstallPhase::Preflight,
                                             UninstallPhase::AwaitingBlockers,
                                             UninstallPhase::RemovingShortcuts,
                                             UninstallPhase::RemovingFiles,
                                             UninstallPhase::PurgingData,
                                             UninstallPhase::HandingOff,
                                             UninstallPhase::Done,
                                             UninstallPhase::Failed};
    const std::vector<UninstallEvent> events{
        uev::Begin{recorded(), false},
        uev::PreflightOk{},
        uev::PreflightFail{SetupError::NothingInstalled},
        uev::BlockersFound{QVector<ProcInfo>{}},
        uev::BlockersGone{},
        uev::CloseAppsRequested{true},
        uev::EffectOk{tag::removeFile, 0},
        uev::EffectFail{tag::removeFile, SetupError::FileOpFailed, QStringLiteral("C:/App")},
        uev::CancelRequested{},
    };

    for (const UninstallPhase phase : phases) {
        for (const UninstallEvent& event : events) {
            const auto r = reduce(state(phase), event);
            if (r.next.has_value()) {
                CHECK(static_cast<int>(r.next->phase) >= static_cast<int>(UninstallPhase::Idle));
                CHECK(static_cast<int>(r.next->phase) <= static_cast<int>(UninstallPhase::Failed));
            } else {
                CHECK(r.effects.size() <= 1);
            }
        }
    }
}
