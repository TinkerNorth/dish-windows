// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Binding presence gate — the rule that stops the app reporting a controller
// that does not exist. A slot->connection binding declares a virtual pad on the
// satellite (ConnectionHub::bind -> WifiConnection::attachSlot), and EVERY
// session PUT re-sends the whole desired-descriptor set. Nothing pruned that set
// when the physical pad went away, so a reconnect re-plugged a virtual pad with
// no hardware behind it — the phantom.
//
// The gate is a pure decision over (the slots the app currently shows) x (the
// bindings the hub holds): keep a binding whose pad is present, MIGRATE one
// whose pad merely moved to its twin id (the USB-direct claim retires the
// framework id and publishes a synthetic in its place — the pad is still
// there), and UNBIND one whose pad is genuinely gone. Pure, Qt-free,
// socket-free.
//
// Local variables here are never named `slots`: Qt's moc keyword macro claims
// that identifier, and these tests share a translation-unit vocabulary with the
// Qt-including seed test next door.

#include "core/reducer/BindingPresence.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

namespace reducer = dish::reducer;
using reducer::BindingPresenceKind;
using reducer::BoundSlot;
using reducer::PresentSlot;
using reducer::resolveBindingPresence;

namespace {

// A slot the app currently shows, with the pad identity behind it.
PresentSlot present(const std::string& id, int vid, int pid) {
    PresentSlot s;
    s.id = id;
    s.vendorId = vid;
    s.productId = pid;
    return s;
}

// A binding the hub holds. vid/pid < 0 means the app can no longer resolve the
// pad behind it at all (the device is not enumerated anywhere).
BoundSlot bound(const std::string& slotId, const std::string& connId, int vid = -1, int pid = -1) {
    BoundSlot b;
    b.slotId = slotId;
    b.connId = connId;
    if (vid >= 0 && pid >= 0) { b.identity = std::make_pair(vid, pid); }
    return b;
}

// The Nintendo Switch Pro Controller (057e:2009) — the pad on the reporting
// machine, and the vpKey its USB-direct synthetic slot id is built from.
constexpr int kSwitchProVid = 0x057e;
constexpr int kSwitchProPid = 0x2009;
const std::string kSwitchProSynthetic = "92151817"; // (0x057e << 16) | 0x2009

} // namespace

TEST_CASE("resolveBindingPresence: a binding whose pad is present is left alone",
          "[bindingpresence]") {
    const std::vector<PresentSlot> shown = {present("sdl:0", kSwitchProVid, kSwitchProPid)};
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-1", kSwitchProVid, kSwitchProPid)};
    REQUIRE(resolveBindingPresence(shown, bindings).empty());
}

TEST_CASE("resolveBindingPresence: a binding whose pad is gone is unbound", "[bindingpresence]") {
    // THE PHANTOM. The pad was unplugged, so it is in no slot list any more and
    // its identity no longer resolves — but the binding (and therefore the
    // descriptor every session PUT re-sends) survived.
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-1")};
    const auto actions = resolveBindingPresence(/*present=*/{}, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Unbind);
    REQUIRE(actions[0].slotId == "sdl:0");
}

TEST_CASE("resolveBindingPresence: an unbind names the connection it dropped",
          "[bindingpresence]") {
    // The notice channel: AppModel turns an Unbind into a one-shot toast that
    // names the satellite the binding was serving, so the user sees a REASON for
    // the slot card going quiet instead of a binding that silently disappears.
    // The connection therefore rides every action, not just a Migrate.
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-living-room")};
    const auto actions = resolveBindingPresence(/*present=*/{}, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Unbind);
    REQUIRE(actions[0].connId == "sat-living-room");
    // Nothing to migrate onto — the target stays empty so a caller cannot
    // mistake it for a live slot id.
    REQUIRE(actions[0].toSlotId.empty());
}

TEST_CASE("resolveBindingPresence: a pad gone while others remain is still unbound",
          "[bindingpresence]") {
    // A second pad being present must not vouch for the departed one.
    const std::vector<PresentSlot> shown = {present("sdl:1", 0x20d6, 0xa713)};
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-1", kSwitchProVid, kSwitchProPid),
                                             bound("sdl:1", "sat-2", 0x20d6, 0xa713)};
    const auto actions = resolveBindingPresence(shown, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Unbind);
    REQUIRE(actions[0].slotId == "sdl:0");
}

TEST_CASE("resolveBindingPresence: a pad that moved onto its USB-direct synthetic migrates",
          "[bindingpresence]") {
    // The claim hides the framework twin and publishes a synthetic in its place.
    // The pad IS still there, so the binding follows it instead of being dropped
    // (dropping it would tear down a working stream on every path switch).
    const std::vector<PresentSlot> shown = {
        present(kSwitchProSynthetic, kSwitchProVid, kSwitchProPid)};
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-1", kSwitchProVid, kSwitchProPid)};
    const auto actions = resolveBindingPresence(shown, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Migrate);
    REQUIRE(actions[0].slotId == "sdl:0");
    REQUIRE(actions[0].toSlotId == kSwitchProSynthetic);
    REQUIRE(actions[0].connId == "sat-1");
}

TEST_CASE("resolveBindingPresence: a synthetic released back to the framework twin migrates back",
          "[bindingpresence]") {
    const std::vector<PresentSlot> shown = {present("sdl:0", kSwitchProVid, kSwitchProPid)};
    const std::vector<BoundSlot> bindings = {
        bound(kSwitchProSynthetic, "sat-1", kSwitchProVid, kSwitchProPid)};
    const auto actions = resolveBindingPresence(shown, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Migrate);
    REQUIRE(actions[0].toSlotId == "sdl:0");
}

TEST_CASE("resolveBindingPresence: an identity-less binding never migrates onto a stranger",
          "[bindingpresence]") {
    // vid/pid 0:0 is identity-LESS (SDL could not read the descriptor), not a
    // key — it must not match a present pad that also reads 0:0. Mirrors the
    // EmulateSeed vidPidKey rule.
    const std::vector<PresentSlot> shown = {present("sdl:9", 0, 0)};
    const std::vector<BoundSlot> bindings = {bound("sdl:0", "sat-1", 0, 0)};
    const auto actions = resolveBindingPresence(shown, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Unbind);
}

TEST_CASE("resolveBindingPresence: a migration target that is already bound is not clobbered",
          "[bindingpresence]") {
    // Same model plugged twice: the departed slot must not steal the surviving
    // twin's binding. Drop the stale one instead.
    const std::vector<PresentSlot> shown = {
        present(kSwitchProSynthetic, kSwitchProVid, kSwitchProPid)};
    const std::vector<BoundSlot> bindings = {
        bound("sdl:0", "sat-1", kSwitchProVid, kSwitchProPid),
        bound(kSwitchProSynthetic, "sat-2", kSwitchProVid, kSwitchProPid)};
    const auto actions = resolveBindingPresence(shown, bindings);
    REQUIRE(actions.size() == 1);
    REQUIRE(actions[0].kind == BindingPresenceKind::Unbind);
    REQUIRE(actions[0].slotId == "sdl:0");
}

TEST_CASE("resolveBindingPresence: no bindings and nothing shown is a no-op", "[bindingpresence]") {
    REQUIRE(resolveBindingPresence({}, {}).empty());
}

TEST_CASE("resolveBindingPresence: actions are ordered by the departing slot id",
          "[bindingpresence]") {
    const std::vector<BoundSlot> bindings = {bound("sdl:2", "sat-c"), bound("sdl:0", "sat-a"),
                                             bound("sdl:1", "sat-b")};
    const auto actions = resolveBindingPresence({}, bindings);
    REQUIRE(actions.size() == 3);
    REQUIRE(actions[0].slotId == "sdl:0");
    REQUIRE(actions[1].slotId == "sdl:1");
    REQUIRE(actions[2].slotId == "sdl:2");
}

TEST_CASE("resolveBindingPresence: the gate is idempotent once applied", "[bindingpresence]") {
    // Re-running over the settled shape yields nothing — the AppModel rebuild
    // re-enters after applying the actions, and a second round of unbinds would
    // loop forever.
    const std::vector<PresentSlot> shown = {
        present(kSwitchProSynthetic, kSwitchProVid, kSwitchProPid)};
    const std::vector<BoundSlot> settled = {
        bound(kSwitchProSynthetic, "sat-1", kSwitchProVid, kSwitchProPid)};
    REQUIRE(resolveBindingPresence(shown, settled).empty());
}
