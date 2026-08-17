// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The install reducer, phase x event. Effect lists are asserted as ORDERED
// vectors because the coordinator executes them in sequence: a reordering is a
// behaviour change, not a cleanup. The four ordering invariants of spec 3.5
// (journal before the effect it covers, ARP last, cancel ignored inside the
// commit window, EffectFail with journal entries rolls back) get their own
// cases so a regression names itself.

#include "installer/InstallMachine.h"

#include <catch2/catch_test_macros.hpp>

#include <QVector>

#include <variant>
#include <vector>

using dish::installer::ExitCode;
using dish::installer::InstallEffect;
using dish::installer::InstallEvent;
using dish::installer::InstallPhase;
using dish::installer::InstallPlan;
using dish::installer::InstallState;
using dish::installer::JournalAction;
using dish::installer::JournalEntry;
using dish::installer::kInstallDiskMargin;
using dish::installer::makeArpValues;
using dish::installer::PayloadEntry;
using dish::installer::PayloadManifest;
using dish::installer::ProcInfo;
using dish::installer::reduce;
using dish::installer::Scope;
using dish::installer::SetupError;
using dish::installer::ShortcutLocation;
using dish::installer::ShortcutSpec;
namespace ev = dish::installer::event;
namespace fx = dish::installer::effect;
namespace tag = dish::installer::effecttag;

namespace {

PayloadEntry entry(const QString& path, qint64 size) {
    PayloadEntry e;
    e.path = path;
    e.stagedAs = path;
    e.size = size;
    e.sha256Hex = QByteArray(64, 'a');
    return e;
}

// Two files, one of them nested, so EnsureDir has a subdirectory to emit.
PayloadManifest manifest() {
    PayloadManifest m;
    m.version = QStringLiteral("1.2.3");
    m.files = QVector<PayloadEntry>{entry(QStringLiteral("dish.exe"), 10),
                                    entry(QStringLiteral("licenses/LICENSE.txt"), 5)};
    m.totalBytes = 15;
    return m;
}

InstallPlan plan(bool upgrade = false) {
    InstallPlan p;
    p.installDir = QStringLiteral("C:/App");
    p.isUpgrade = upgrade;
    return p;
}

InstallState state(InstallPhase phase, const InstallPlan& p = plan()) {
    InstallState s;
    s.phase = phase;
    s.plan = p;
    return s;
}

JournalEntry journal(JournalAction action, const QString& path, const QString& aux = QString()) {
    JournalEntry e;
    e.action = action;
    e.path = path;
    e.aux = aux;
    return e;
}

ShortcutSpec spec(ShortcutLocation location, Scope scope = Scope::PerUser) {
    ShortcutSpec s;
    s.location = location;
    s.scope = scope;
    s.targetAbs = QStringLiteral("C:/App/dish.exe");
    s.workingDir = QStringLiteral("C:/App");
    s.iconAbs = QStringLiteral("C:/App/dish.exe");
    s.iconIndex = 0;
    s.description = QStringLiteral("Dish");
    return s;
}

template <class T> int indexOf(const std::vector<InstallEffect>& effects) {
    for (std::size_t i = 0; i < effects.size(); ++i) {
        if (std::holds_alternative<T>(effects.at(i))) { return static_cast<int>(i); }
    }
    return -1;
}

template <class T> int countOf(const std::vector<InstallEffect>& effects) {
    int n = 0;
    for (const InstallEffect& effect : effects) {
        if (std::holds_alternative<T>(effect)) { ++n; }
    }
    return n;
}

// Every mutating effect must be immediately preceded by the journal line that
// covers it. DeleteStale is the one exemption: its enumeration (and its journal
// lines) belong to the executor, which owns the OLD manifest.
bool journalPrecedesEveryMutation(const std::vector<InstallEffect>& effects) {
    for (std::size_t i = 0; i < effects.size(); ++i) {
        const InstallEffect& effect = effects.at(i);
        const bool mutating = std::holds_alternative<fx::EnsureDir>(effect) ||
                              std::holds_alternative<fx::CopyVerify>(effect) ||
                              std::holds_alternative<fx::StageOld>(effect) ||
                              std::holds_alternative<fx::PromoteStaged>(effect) ||
                              std::holds_alternative<fx::WriteInstalledManifest>(effect) ||
                              std::holds_alternative<fx::CreateShortcut>(effect) ||
                              std::holds_alternative<fx::WriteArp>(effect);
        if (!mutating) { continue; }
        if (i == 0 || !std::holds_alternative<fx::WriteJournalEntry>(effects.at(i - 1))) {
            return false;
        }
    }
    return true;
}

InstallState begunState(const InstallPlan& p) {
    const auto r = reduce(state(InstallPhase::Idle, p), ev::Begin{p, manifest()});
    return r.next.value();
}

} // namespace

// ── Begin: the whole forward pipeline, in order ─────────────────────────────

TEST_CASE("installer machine: Begin from Idle emits the fresh-install pipeline in order",
          "[installer][install-fsm]") {
    const InstallPlan p = plan();
    const auto r = reduce(state(InstallPhase::Idle, p), ev::Begin{p, manifest()});

    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Preflight);
    CHECK(r.next->plan == p);
    CHECK(r.next->bytesTotal == 15);
    CHECK(r.next->bytesDone == 0);
    CHECK(r.next->rollbackCursor == 0);

    const std::vector<InstallEffect> expected{
        fx::ProbeDisk{QStringLiteral("C:/App"), 15 + kInstallDiskMargin},
        fx::ScanProcesses{QStringLiteral("C:/App")},
        fx::WriteJournalEntry{journal(JournalAction::CreatedDir, QStringLiteral("C:/App"))},
        fx::EnsureDir{QStringLiteral("C:/App")},
        fx::WriteJournalEntry{
            journal(JournalAction::CreatedDir, QStringLiteral("C:/App/licenses"))},
        fx::EnsureDir{QStringLiteral("C:/App/licenses")},
        fx::WriteJournalEntry{
            journal(JournalAction::CopiedFile, QStringLiteral("C:/App/dish.exe"))},
        fx::CopyVerify{0, QStringLiteral("dish.exe"), QStringLiteral("C:/App/dish.exe")},
        fx::WriteJournalEntry{
            journal(JournalAction::CopiedFile, QStringLiteral("C:/App/licenses/LICENSE.txt"))},
        fx::CopyVerify{1, QStringLiteral("licenses/LICENSE.txt"),
                       QStringLiteral("C:/App/licenses/LICENSE.txt")},
        fx::WriteJournalEntry{
            journal(JournalAction::WroteManifest, QStringLiteral("C:/App/.dish-manifest.json"))},
        fx::WriteInstalledManifest{},
        fx::WriteJournalEntry{
            journal(JournalAction::CreatedShortcut, QString(), QStringLiteral("startmenu|user"))},
        fx::CreateShortcut{spec(ShortcutLocation::StartMenu)},
        fx::WriteJournalEntry{journal(JournalAction::WroteArp, QString(), QStringLiteral("user"))},
        fx::WriteArp{makeArpValues(p, QStringLiteral("1.2.3"), 15)},
        fx::CommitCleanup{},
        fx::Finish{ExitCode::Ok},
    };
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: the upgrade pipeline stages, re-scans and commits by rename",
          "[installer][install-fsm]") {
    const InstallPlan p = plan(/*upgrade=*/true);
    const auto r = reduce(state(InstallPhase::Idle, p), ev::Begin{p, manifest()});

    const std::vector<InstallEffect> expected{
        fx::ProbeDisk{QStringLiteral("C:/App"), 15 + kInstallDiskMargin},
        fx::ScanProcesses{QStringLiteral("C:/App")},
        fx::WriteJournalEntry{
            journal(JournalAction::CreatedDir, QStringLiteral("C:/App/.dish-stage"))},
        fx::EnsureDir{QStringLiteral("C:/App/.dish-stage")},
        fx::WriteJournalEntry{
            journal(JournalAction::CreatedDir, QStringLiteral("C:/App/.dish-stage/licenses"))},
        fx::EnsureDir{QStringLiteral("C:/App/.dish-stage/licenses")},
        fx::WriteJournalEntry{
            journal(JournalAction::CopiedFile, QStringLiteral("C:/App/.dish-stage/dish.exe"))},
        fx::CopyVerify{0, QStringLiteral("dish.exe"),
                       QStringLiteral("C:/App/.dish-stage/dish.exe")},
        fx::WriteJournalEntry{journal(JournalAction::CopiedFile,
                                      QStringLiteral("C:/App/.dish-stage/licenses/LICENSE.txt"))},
        fx::CopyVerify{1, QStringLiteral("licenses/LICENSE.txt"),
                       QStringLiteral("C:/App/.dish-stage/licenses/LICENSE.txt")},
        // The commit window opens with a fresh blocker scan (spec 11.2).
        fx::ScanProcesses{QStringLiteral("C:/App")},
        fx::WriteJournalEntry{journal(JournalAction::StagedOld,
                                      QStringLiteral("C:/App/.dish-manifest.json"),
                                      QStringLiteral("C:/App/.dish-old/.dish-manifest.json"))},
        fx::StageOld{QStringLiteral(".dish-manifest.json")},
        fx::WriteJournalEntry{journal(JournalAction::StagedOld, QStringLiteral("C:/App/dish.exe"),
                                      QStringLiteral("C:/App/.dish-old/dish.exe"))},
        fx::StageOld{QStringLiteral("dish.exe")},
        fx::WriteJournalEntry{journal(JournalAction::PromotedStaged,
                                      QStringLiteral("C:/App/dish.exe"),
                                      QStringLiteral("C:/App/.dish-stage/dish.exe"))},
        fx::PromoteStaged{QStringLiteral("dish.exe")},
        fx::WriteJournalEntry{journal(JournalAction::StagedOld,
                                      QStringLiteral("C:/App/licenses/LICENSE.txt"),
                                      QStringLiteral("C:/App/.dish-old/licenses/LICENSE.txt"))},
        fx::StageOld{QStringLiteral("licenses/LICENSE.txt")},
        fx::WriteJournalEntry{journal(JournalAction::PromotedStaged,
                                      QStringLiteral("C:/App/licenses/LICENSE.txt"),
                                      QStringLiteral("C:/App/.dish-stage/licenses/LICENSE.txt"))},
        fx::PromoteStaged{QStringLiteral("licenses/LICENSE.txt")},
        // Empty relPath: "every file in the OLD manifest absent from the new".
        fx::DeleteStale{QString()},
        fx::WriteJournalEntry{
            journal(JournalAction::WroteManifest, QStringLiteral("C:/App/.dish-manifest.json"))},
        fx::WriteInstalledManifest{},
        fx::WriteJournalEntry{
            journal(JournalAction::CreatedShortcut, QString(), QStringLiteral("startmenu|user"))},
        fx::CreateShortcut{spec(ShortcutLocation::StartMenu)},
        fx::WriteJournalEntry{journal(JournalAction::WroteArp, QString(), QStringLiteral("user"))},
        fx::WriteArp{makeArpValues(p, QStringLiteral("1.2.3"), 15)},
        fx::CommitCleanup{},
        fx::Finish{ExitCode::Ok},
    };
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: every mutation is preceded by its journal line",
          "[installer][install-fsm]") {
    CHECK(journalPrecedesEveryMutation(
        reduce(state(InstallPhase::Idle), ev::Begin{plan(), manifest()}).effects));

    InstallPlan upgrade = plan(/*upgrade=*/true);
    upgrade.desktop = true;
    CHECK(journalPrecedesEveryMutation(
        reduce(state(InstallPhase::Idle, upgrade), ev::Begin{upgrade, manifest()}).effects));
}

TEST_CASE("installer machine: ARP is the last mutating effect", "[installer][install-fsm]") {
    InstallPlan p = plan();
    p.desktop = true;
    p.launch = true;
    const auto r = reduce(state(InstallPhase::Idle, p), ev::Begin{p, manifest()});

    const int arp = indexOf<fx::WriteArp>(r.effects);
    REQUIRE(arp >= 0);
    CHECK(arp > indexOf<fx::WriteInstalledManifest>(r.effects));
    CHECK(arp > indexOf<fx::CreateShortcut>(r.effects));
    CHECK(arp < indexOf<fx::CommitCleanup>(r.effects));
    // Nothing that touches the install after it but the cleanup, the launch and
    // the exit: a valid ARP entry therefore implies a complete install.
    for (std::size_t i = static_cast<std::size_t>(arp) + 1; i < r.effects.size(); ++i) {
        const InstallEffect& effect = r.effects.at(i);
        CHECK((std::holds_alternative<fx::CommitCleanup>(effect) ||
               std::holds_alternative<fx::LaunchApp>(effect) ||
               std::holds_alternative<fx::Finish>(effect)));
    }
}

TEST_CASE("installer machine: shortcut effects follow the plan's two switches",
          "[installer][install-fsm]") {
    InstallPlan none = plan();
    none.startMenu = false;
    none.desktop = false;
    CHECK(countOf<fx::CreateShortcut>(
              reduce(state(InstallPhase::Idle, none), ev::Begin{none, manifest()}).effects) == 0);

    InstallPlan both = plan();
    both.desktop = true;
    both.scope = Scope::AllUsers;
    const auto r = reduce(state(InstallPhase::Idle, both), ev::Begin{both, manifest()});
    REQUIRE(countOf<fx::CreateShortcut>(r.effects) == 2);
    const int startMenu = indexOf<fx::CreateShortcut>(r.effects);
    CHECK(std::get<fx::CreateShortcut>(r.effects.at(static_cast<std::size_t>(startMenu))).spec ==
          spec(ShortcutLocation::StartMenu, Scope::AllUsers));
    // The desktop pair is journaled with the machine-scope token.
    CHECK(std::get<fx::WriteJournalEntry>(r.effects.at(static_cast<std::size_t>(startMenu) + 1))
              .entry.aux == QStringLiteral("desktop|machine"));
}

TEST_CASE("installer machine: launch on appends LaunchApp before Finish",
          "[installer][install-fsm]") {
    InstallPlan p = plan();
    p.launch = true;
    const auto r = reduce(state(InstallPhase::Idle, p), ev::Begin{p, manifest()});
    const int launch = indexOf<fx::LaunchApp>(r.effects);
    REQUIRE(launch >= 0);
    CHECK(std::get<fx::LaunchApp>(r.effects.at(static_cast<std::size_t>(launch))).deElevate);
    CHECK(launch == indexOf<fx::Finish>(r.effects) - 1);
}

// ── Idle ────────────────────────────────────────────────────────────────────

TEST_CASE("installer machine: Idle plus ElevationRequested detours to the elevated relaunch",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Idle), ev::ElevationRequested{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::AwaitingElevation);
    // Empty exe/argv: the coordinator substitutes the recorded original exe and
    // the serialized options, which the reducer never learns.
    const std::vector<InstallEffect> expected{fx::RelaunchElevated{QString(), QStringList()}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: Idle ignores every mid-run event", "[installer][install-fsm]") {
    for (const InstallEvent& event :
         {InstallEvent{ev::PreflightOk{100}}, InstallEvent{ev::BlockersGone{}},
          InstallEvent{ev::CopyProgress{5}}, InstallEvent{ev::CancelRequested{}},
          InstallEvent{ev::EffectOk{tag::copyVerify, 0}},
          InstallEvent{ev::RollbackStepDone{0, true}}}) {
        const auto r = reduce(state(InstallPhase::Idle), event);
        CHECK_FALSE(r.next.has_value());
        CHECK(r.effects.empty());
    }
}

// ── Preflight ───────────────────────────────────────────────────────────────

TEST_CASE("installer machine: PreflightOk is a pass-through", "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Preflight), ev::PreflightOk{999});
    CHECK_FALSE(r.next.has_value());
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: PreflightFail fails typed with the install dir",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Preflight), ev::PreflightFail{SetupError::DiskFull});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Failed);
    CHECK(r.next->error == SetupError::DiskFull);
    CHECK(r.next->errorPath == QStringLiteral("C:/App"));
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::DiskFull}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: blockers found under each close policy", "[installer][install-fsm]") {
    const QVector<ProcInfo> procs{
        ProcInfo{4242, QStringLiteral("C:/App/dish.exe"), QStringLiteral("dish.exe")}};

    SECTION("abort waits for the UI and emits nothing") {
        const auto r = reduce(state(InstallPhase::Preflight), ev::BlockersFound{procs});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == InstallPhase::AwaitingBlockers);
        CHECK(r.effects.empty());
    }
    SECTION("graceful closes with a 10 s grace") {
        InstallPlan p = plan();
        p.closePolicy = dish::installer::ClosePolicy::Graceful;
        const auto r = reduce(state(InstallPhase::Preflight, p), ev::BlockersFound{procs});
        const std::vector<InstallEffect> expected{fx::CloseProcesses{procs, false, 10000}};
        CHECK(r.effects == expected);
    }
    SECTION("force terminates after the same grace") {
        InstallPlan p = plan();
        p.closePolicy = dish::installer::ClosePolicy::Force;
        const auto r = reduce(state(InstallPhase::Preflight, p), ev::BlockersFound{procs});
        const std::vector<InstallEffect> expected{fx::CloseProcesses{procs, true, 10000}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("installer machine: BlockersGone opens the copy phase", "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Preflight), ev::BlockersGone{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Copying);
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: cancel before anything was journaled fails clean",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Preflight), ev::CancelRequested{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Failed);
    CHECK(r.next->error == SetupError::Cancelled);
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::Cancelled}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: EffectFail with an empty journal fails instead of rolling back",
          "[installer][install-fsm]") {
    const auto r =
        reduce(state(InstallPhase::Preflight),
               ev::EffectFail{tag::probeDisk, SetupError::DiskFull, QStringLiteral("C:/App")});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Failed);
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::DiskFull}};
    CHECK(r.effects == expected);
}

// ── AwaitingBlockers ────────────────────────────────────────────────────────

TEST_CASE("installer machine: CloseAppsRequested re-snapshots the process set",
          "[installer][install-fsm]") {
    SECTION("graceful") {
        const auto r = reduce(state(InstallPhase::AwaitingBlockers), ev::CloseAppsRequested{false});
        CHECK_FALSE(r.next.has_value());
        // Empty procs: the executor re-snapshots, which closes the TOCTOU
        // between the scan and the close.
        const std::vector<InstallEffect> expected{
            fx::CloseProcesses{QVector<ProcInfo>(), false, 10000}};
        CHECK(r.effects == expected);
    }
    SECTION("force") {
        const auto r = reduce(state(InstallPhase::AwaitingBlockers), ev::CloseAppsRequested{true});
        const std::vector<InstallEffect> expected{
            fx::CloseProcesses{QVector<ProcInfo>(), true, 10000}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("installer machine: a survived close does not re-close automatically",
          "[installer][install-fsm]") {
    const QVector<ProcInfo> procs{
        ProcInfo{7, QStringLiteral("C:/App/dish.exe"), QStringLiteral("dish.exe")}};
    const auto r = reduce(state(InstallPhase::AwaitingBlockers), ev::BlockersFound{procs});
    CHECK_FALSE(r.next.has_value());
    CHECK(r.effects.empty()); // the UI swaps to Force / Retry; a loop here would spin
}

TEST_CASE("installer machine: AwaitingBlockers resolves into Copying", "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::AwaitingBlockers), ev::BlockersGone{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Copying);
}

TEST_CASE("installer machine: cancel while blocked fails cancelled", "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::AwaitingBlockers), ev::CancelRequested{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Failed);
    CHECK(r.next->error == SetupError::Cancelled);
}

// ── AwaitingElevation ───────────────────────────────────────────────────────

TEST_CASE("installer machine: a spawned elevated instance ends this one at Ok",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::AwaitingElevation), ev::ElevationSpawned{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Done);
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::Ok}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: a declined UAC re-arms the wizard, never a prompt loop",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::AwaitingElevation);
    s.error = SetupError::NeedElevation;
    s.errorPath = QStringLiteral("C:/App");

    const auto declined = reduce(s, ev::ElevationDeclined{});
    REQUIRE(declined.next.has_value());
    CHECK(declined.next->phase == InstallPhase::Idle);
    CHECK(declined.next->error == SetupError::None);
    CHECK(declined.next->errorPath.isEmpty());
    CHECK(declined.effects.empty());

    // A failed spawn lands in the same place.
    const auto failed = reduce(s, ev::EffectFail{tag::relaunchElevated, SetupError::NeedElevation,
                                                 QStringLiteral("C:/setup.exe")});
    REQUIRE(failed.next.has_value());
    CHECK(failed.next->phase == InstallPhase::Idle);
}

// ── Copying ─────────────────────────────────────────────────────────────────

TEST_CASE("installer machine: journal acknowledgements advance the rollback cursor",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Copying);
    s.rollbackCursor = 3;
    const auto r = reduce(s, ev::EffectOk{tag::writeJournalEntry, 0});
    REQUIRE(r.next.has_value());
    CHECK(r.next->rollbackCursor == 4);
    CHECK(r.next->phase == InstallPhase::Copying);
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: copy acknowledgements report a 1-based file index",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Copying), ev::EffectOk{tag::copyVerify, 3});
    REQUIRE(r.next.has_value());
    CHECK(r.next->fileIndex == 4);
}

TEST_CASE("installer machine: copy progress accumulates bytes", "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Copying);
    s.bytesDone = 100;
    const auto r = reduce(s, ev::CopyProgress{40});
    REQUIRE(r.next.has_value());
    CHECK(r.next->bytesDone == 140);
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: a fresh install's manifest write opens Finalizing",
          "[installer][install-fsm]") {
    const auto r =
        reduce(state(InstallPhase::Copying), ev::EffectOk{tag::writeInstalledManifest, 0});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::Finalizing);
}

TEST_CASE("installer machine: the upgrade commit gate moves Copying into Committing",
          "[installer][install-fsm]") {
    const InstallPlan p = plan(/*upgrade=*/true);
    SECTION("clear") {
        const auto r = reduce(state(InstallPhase::Copying, p), ev::BlockersGone{});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == InstallPhase::Committing);
        CHECK(r.effects.empty());
    }
    SECTION("blocked, with a close policy") {
        InstallPlan graceful = p;
        graceful.closePolicy = dish::installer::ClosePolicy::Graceful;
        const QVector<ProcInfo> procs{
            ProcInfo{9, QStringLiteral("C:/App/dish.exe"), QStringLiteral("dish.exe")}};
        const auto r = reduce(state(InstallPhase::Copying, graceful), ev::BlockersFound{procs});
        REQUIRE(r.next.has_value());
        CHECK(r.next->phase == InstallPhase::Committing);
        const std::vector<InstallEffect> expected{fx::CloseProcesses{procs, false, 10000}};
        CHECK(r.effects == expected);
    }
}

TEST_CASE("installer machine: cancel during Copying is recorded, not acted on",
          "[installer][install-fsm]") {
    const auto r = reduce(state(InstallPhase::Copying), ev::CancelRequested{});
    REQUIRE(r.next.has_value());
    CHECK(r.next->cancelRequested);
    CHECK(r.next->phase == InstallPhase::Copying); // the executor aborts the copy in flight
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: a failure with journal entries rolls back",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Copying);
    s.rollbackCursor = 5;
    const auto r = reduce(s, ev::EffectFail{tag::copyVerify, SetupError::FileOpFailed,
                                            QStringLiteral("C:/App/dish.exe")});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::RollingBack);
    CHECK(r.next->error == SetupError::FileOpFailed);
    CHECK(r.next->errorPath == QStringLiteral("C:/App/dish.exe"));
    CHECK(r.next->rollbackCursor == 5); // the coordinator replays from here, in reverse
    CHECK(r.effects.empty());
}

// ── Committing ──────────────────────────────────────────────────────────────

TEST_CASE("installer machine: cancel is ignored inside the commit window",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Committing, plan(/*upgrade=*/true));
    s.rollbackCursor = 12;
    const auto r = reduce(s, ev::CancelRequested{});
    CHECK_FALSE(r.next.has_value());
    CHECK(r.effects.empty());
}

TEST_CASE("installer machine: Committing acknowledgements", "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Committing, plan(/*upgrade=*/true));
    s.rollbackCursor = 2;

    const auto journalOk = reduce(s, ev::EffectOk{tag::writeJournalEntry, 0});
    REQUIRE(journalOk.next.has_value());
    CHECK(journalOk.next->rollbackCursor == 3);

    const auto renameOk = reduce(s, ev::EffectOk{tag::promoteStaged, 0});
    CHECK_FALSE(renameOk.next.has_value()); // renames do not move the phase

    const auto manifestOk = reduce(s, ev::EffectOk{tag::writeInstalledManifest, 0});
    REQUIRE(manifestOk.next.has_value());
    CHECK(manifestOk.next->phase == InstallPhase::Finalizing);

    const auto close = reduce(s, ev::CloseAppsRequested{true});
    const std::vector<InstallEffect> expected{fx::CloseProcesses{QVector<ProcInfo>(), true, 10000}};
    CHECK(close.effects == expected);

    const auto fail = reduce(s, ev::EffectFail{tag::promoteStaged, SetupError::FileOpFailed,
                                               QStringLiteral("C:/App/dish.exe")});
    REQUIRE(fail.next.has_value());
    CHECK(fail.next->phase == InstallPhase::RollingBack);
}

// ── Finalizing ──────────────────────────────────────────────────────────────

TEST_CASE("installer machine: Finalizing ignores cancel and ends at Done",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Finalizing);
    s.rollbackCursor = 20;

    CHECK_FALSE(reduce(s, ev::CancelRequested{}).next.has_value());

    const auto journalOk = reduce(s, ev::EffectOk{tag::writeJournalEntry, 0});
    REQUIRE(journalOk.next.has_value());
    CHECK(journalOk.next->rollbackCursor == 21);

    const auto done = reduce(s, ev::EffectOk{tag::finish, 0});
    REQUIRE(done.next.has_value());
    CHECK(done.next->phase == InstallPhase::Done);
    CHECK(done.effects.empty());
}

TEST_CASE("installer machine: a registry failure at the end still rolls back",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Finalizing);
    s.rollbackCursor = 20;
    const auto r = reduce(
        s, ev::EffectFail{tag::writeArp, SetupError::RegistryFailed, QStringLiteral("HKCU")});
    REQUIRE(r.next.has_value());
    CHECK(r.next->phase == InstallPhase::RollingBack);
    CHECK(r.next->error == SetupError::RegistryFailed);
}

// ── RollingBack ─────────────────────────────────────────────────────────────

TEST_CASE("installer machine: the rollback replay walks the cursor down to zero",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::RollingBack);
    s.error = SetupError::FileOpFailed;
    s.rollbackCursor = 3;

    const auto step = reduce(s, ev::RollbackStepDone{2, true});
    REQUIRE(step.next.has_value());
    CHECK(step.next->phase == InstallPhase::RollingBack);
    CHECK(step.next->rollbackCursor == 2);
    CHECK(step.effects.empty());

    InstallState last = s;
    last.rollbackCursor = 1;
    const auto done = reduce(last, ev::RollbackStepDone{0, true});
    REQUIRE(done.next.has_value());
    CHECK(done.next->phase == InstallPhase::Failed);
    // A clean replay reports the OUTCOME (rolled back), not the trigger.
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::RolledBack}};
    CHECK(done.effects == expected);
}

TEST_CASE("installer machine: one missed undo turns exit 8 into exit 9",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::RollingBack);
    s.error = SetupError::FileOpFailed;
    s.rollbackCursor = 2;

    const auto missed = reduce(s, ev::RollbackStepDone{1, false});
    REQUIRE(missed.next.has_value());
    CHECK(missed.next->error == SetupError::RollbackIncomplete);

    const auto done = reduce(*missed.next, ev::RollbackStepDone{0, true});
    REQUIRE(done.next.has_value());
    CHECK(done.next->phase == InstallPhase::Failed);
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::RollbackIncomplete}};
    CHECK(done.effects == expected);
}

TEST_CASE("installer machine: a cancelled install that rolled back cleanly still exits 10",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::RollingBack);
    s.error = SetupError::Cancelled;
    s.rollbackCursor = 1;
    const auto r = reduce(s, ev::RollbackStepDone{0, true});
    const std::vector<InstallEffect> expected{fx::Finish{ExitCode::Cancelled}};
    CHECK(r.effects == expected);
}

TEST_CASE("installer machine: RollingBack ignores cancel and stray acknowledgements",
          "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::RollingBack);
    s.rollbackCursor = 4;
    CHECK_FALSE(reduce(s, ev::CancelRequested{}).next.has_value());
    CHECK_FALSE(reduce(s, ev::EffectOk{tag::copyVerify, 1}).next.has_value());
    CHECK_FALSE(reduce(s, ev::CopyProgress{10}).next.has_value());
}

// ── Terminal phases ─────────────────────────────────────────────────────────

TEST_CASE("installer machine: Done is terminal", "[installer][install-fsm]") {
    for (const InstallEvent& event :
         {InstallEvent{ev::CancelRequested{}}, InstallEvent{ev::Begin{plan(), manifest()}},
          InstallEvent{ev::BlockersGone{}}, InstallEvent{ev::EffectOk{tag::finish, 0}}}) {
        const auto r = reduce(state(InstallPhase::Done), event);
        CHECK_FALSE(r.next.has_value());
        CHECK(r.effects.empty());
    }
}

TEST_CASE("installer machine: Failed re-arms only on a fresh Begin", "[installer][install-fsm]") {
    InstallState s = state(InstallPhase::Failed);
    s.error = SetupError::FileOpFailed;
    s.rollbackCursor = 7;

    CHECK_FALSE(reduce(s, ev::CancelRequested{}).next.has_value());
    CHECK_FALSE(reduce(s, ev::BlockersGone{}).next.has_value());

    const auto retry = reduce(s, ev::Begin{plan(), manifest()});
    REQUIRE(retry.next.has_value());
    CHECK(retry.next->phase == InstallPhase::Preflight);
    CHECK(retry.next->error == SetupError::None);
    CHECK(retry.next->rollbackCursor == 0);
    CHECK(retry.effects.size() > 1);
}

// ── Totality ────────────────────────────────────────────────────────────────

TEST_CASE("installer machine: reduce is total over every phase and event",
          "[installer][install-fsm]") {
    const std::vector<InstallPhase> phases{InstallPhase::Idle,
                                           InstallPhase::Preflight,
                                           InstallPhase::AwaitingBlockers,
                                           InstallPhase::AwaitingElevation,
                                           InstallPhase::Copying,
                                           InstallPhase::Committing,
                                           InstallPhase::Finalizing,
                                           InstallPhase::RollingBack,
                                           InstallPhase::Done,
                                           InstallPhase::Failed};
    const std::vector<InstallEvent> events{
        ev::Begin{plan(), manifest()},
        ev::PreflightOk{1},
        ev::PreflightFail{SetupError::DiskFull},
        ev::BlockersFound{QVector<ProcInfo>{}},
        ev::BlockersGone{},
        ev::CloseAppsRequested{false},
        ev::ElevationRequested{},
        ev::ElevationSpawned{},
        ev::ElevationDeclined{},
        ev::EffectOk{tag::copyVerify, 0},
        ev::EffectFail{tag::copyVerify, SetupError::FileOpFailed, QStringLiteral("C:/App")},
        ev::CopyProgress{1},
        ev::CancelRequested{},
        ev::RollbackStepDone{0, true},
    };

    for (const InstallPhase phase : phases) {
        for (const InstallEvent& event : events) {
            InstallState s = state(phase);
            s.rollbackCursor = 2;
            const auto r = reduce(s, event);
            // Total: a reduction always comes back, and a phase it does move to
            // is one of the ten (an uninitialized enum would fail here).
            if (r.next.has_value()) {
                CHECK(static_cast<int>(r.next->phase) >= static_cast<int>(InstallPhase::Idle));
                CHECK(static_cast<int>(r.next->phase) <= static_cast<int>(InstallPhase::Failed));
            } else {
                CHECK(r.effects.size() <= 1);
            }
        }
    }
}

TEST_CASE("installer machine: exitAfterRollback collapses op failures onto exit 8",
          "[installer][install-fsm]") {
    using dish::installer::exitAfterRollback;
    CHECK(exitAfterRollback(SetupError::FileOpFailed) == ExitCode::RolledBack);
    CHECK(exitAfterRollback(SetupError::RegistryFailed) == ExitCode::RolledBack);
    CHECK(exitAfterRollback(SetupError::ShortcutFailed) == ExitCode::RolledBack);
    CHECK(exitAfterRollback(SetupError::Cancelled) == ExitCode::Cancelled);
    CHECK(exitAfterRollback(SetupError::PayloadCorrupt) == ExitCode::PayloadCorrupt);
    CHECK(exitAfterRollback(SetupError::RollbackIncomplete) == ExitCode::RollbackIncomplete);
}

TEST_CASE("installer machine: Begin seeds bytesTotal from the manifest total",
          "[installer][install-fsm]") {
    const InstallState s = begunState(plan());
    CHECK(s.bytesTotal == manifest().totalBytes);
    CHECK(s.fileIndex == 0);
    CHECK_FALSE(s.cancelRequested);
}
