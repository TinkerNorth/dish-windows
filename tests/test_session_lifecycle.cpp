// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "Network/SatelliteClient.h"
#include "Network/WifiConnection.h"
#include "core/model/Protocol.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QString>

#include <memory>

using dish::models::ControllerApplyDto;
using dish::models::DiscoveredServer;
using dish::models::SessionViewControllerDto;
using dish::models::SessionViewDto;
using dish::net::SatelliteClient;
using dish::net::SessionState;
using dish::net::WifiConnection;
namespace proto = dish::proto;

namespace {

DiscoveredServer server(const QString& ip = QStringLiteral("10.0.0.1")) {
    DiscoveredServer s;
    s.machineId = QStringLiteral("m1");
    s.ip = ip;
    s.name = QStringLiteral("Pc");
    s.udpPort = 9876;
    s.pairPort = 9443;
    s.httpPort = 9443;
    return s;
}

WifiConnection makeConn() { return WifiConnection(server().id(), server()); }

ControllerApplyDto apply(int idx, std::uint8_t code, int appliedType = proto::kControllerTypeXbox) {
    ControllerApplyDto a;
    a.ctrlIdx = idx;
    a.resultCode = code;
    a.appliedType = appliedType;
    a.result = QString::fromUtf8(proto::applyResultName(code).data(),
                                 static_cast<int>(proto::applyResultName(code).size()));
    return a;
}

SessionViewControllerDto view(int idx, int appliedType, bool active = true) {
    SessionViewControllerDto c;
    c.ctrlIdx = idx;
    c.appliedType = appliedType;
    c.active = active;
    return c;
}

// QSignalSpy needs Qt6::Test, which this target does not link.
struct SignalCounter {
    int count = 0;
    template <class Sender, class Signal> SignalCounter(Sender* s, Signal sig) {
        QObject::connect(s, sig, [this] { ++count; });
    }
};

} // namespace

TEST_CASE("session: initial state is Idle with no connectionId or slots", "[session]") {
    auto conn = makeConn();
    REQUIRE(conn.state() == SessionState::Idle);
    REQUIRE_FALSE(conn.connectionId().has_value());
    REQUIRE(conn.desiredDescriptors().isEmpty());
    REQUIRE(conn.registeredBitmap() == 0);
}

TEST_CASE("session: idFor derives the stable id from the machineId", "[session]") {
    REQUIRE(WifiConnection::idFor(server()) == QStringLiteral("mid:m1"));
}

TEST_CASE("session: attachSlot records the full descriptor with no default-type phase",
          "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypePlayStation, false, false);
    const auto d = conn.descriptorFor(QStringLiteral("slot-A"));
    REQUIRE(d.has_value());
    REQUIRE(d->type == proto::kControllerTypePlayStation);
    REQUIRE(d->ctrlIdx == 0);
}

TEST_CASE("session: desiredDescriptors folds caps from the slot's hardware flags", "[session]") {
    auto conn = makeConn();
    // Motion + lightbar fold on top of the always-on rumble + analog-trigger base.
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypePlayStation, /*lightbar=*/true,
                    /*motion=*/true);
    const auto ds = conn.desiredDescriptors();
    REQUIRE(ds.size() == 1);
    const std::uint16_t caps = ds[0].caps;
    REQUIRE((caps & proto::kCapAnalogTriggers) != 0);
    REQUIRE((caps & proto::kCapRumble) != 0);
    REQUIRE((caps & proto::kCapMotion) != 0);
    REQUIRE((caps & proto::kCapLightbar) != 0);
}

TEST_CASE("session: an Xbox-style pad (no motion/lightbar) advertises neither cap", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    const auto ds = conn.desiredDescriptors();
    REQUIRE(ds.size() == 1);
    REQUIRE((ds[0].caps & proto::kCapMotion) == 0);
    REQUIRE((ds[0].caps & proto::kCapLightbar) == 0);
    // ...but always rumble + analog triggers.
    REQUIRE((ds[0].caps & proto::kCapRumble) != 0);
    REQUIRE((ds[0].caps & proto::kCapAnalogTriggers) != 0);
}

TEST_CASE("session: a second slot allocates a fresh controller index, lowest-free", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    conn.attachSlot(QStringLiteral("slot-B"), proto::kControllerTypeXbox, false, false);
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-A"))->ctrlIdx == 0);
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-B"))->ctrlIdx == 1);
}

TEST_CASE("session: detaching one slot frees its index for the next attach", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    conn.attachSlot(QStringLiteral("slot-B"), proto::kControllerTypeXbox, false, false);
    conn.detachSlot(QStringLiteral("slot-A")); // frees index 0
    conn.attachSlot(QStringLiteral("slot-C"), proto::kControllerTypeXbox, false, false);
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-C"))->ctrlIdx == 0);
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-B"))->ctrlIdx == 1);
}

TEST_CASE("session: detachSlot for an unknown slot is a no-op", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    conn.detachSlot(QStringLiteral("ghost"));
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-A")).has_value());
    REQUIRE(conn.desiredDescriptors().size() == 1);
}

TEST_CASE("session: slotIdForIndex maps an index back to its slot", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    conn.attachSlot(QStringLiteral("slot-B"), proto::kControllerTypeXbox, false, false);
    REQUIRE(conn.slotIdForIndex(0) == QStringLiteral("slot-A"));
    REQUIRE(conn.slotIdForIndex(1) == QStringLiteral("slot-B"));
    REQUIRE(conn.slotIdForIndex(9).isEmpty());
}

TEST_CASE("session: wantsMouseControl is true iff a slot requests mouse touchpad mode",
          "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    REQUIRE_FALSE(conn.wantsMouseControl());
}

TEST_CASE("session: applyResults flips the registered flag for an ok slot", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    REQUIRE(conn.registeredBitmap() == 0);
    conn.applyResults({apply(0, proto::kApplyOk)});
    REQUIRE(conn.registeredBitmap() == 0x0001);
}

TEST_CASE("session: a failed apply keeps the slot unregistered and surfaces the failure",
          "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    SignalCounter errSpy(&conn, &WifiConnection::errorOccurred);
    conn.applyResults({apply(0, proto::kApplyPluginFailed)});
    REQUIRE(conn.registeredBitmap() == 0);
    REQUIRE(errSpy.count == 1);
}

TEST_CASE("session: replugFailed keeps the slot live (the previous pad keeps streaming)",
          "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    // The previous pad stays in force, so the slot stays registered and its
    // streams keep flowing even though the failure is surfaced.
    SignalCounter errSpy(&conn, &WifiConnection::errorOccurred);
    conn.applyResults({apply(0, proto::kApplyReplugFailed)});
    REQUIRE(conn.registeredBitmap() == 0x0001);
    REQUIRE(errSpy.count == 1);
}

TEST_CASE("session: registeredBitmap sets one bit per registered controller index", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false); // idx 0
    conn.attachSlot(QStringLiteral("slot-B"), proto::kControllerTypeXbox, false, false); // idx 1
    conn.applyResults({apply(0, proto::kApplyOk), apply(1, proto::kApplyOk)});
    REQUIRE(conn.registeredBitmap() == 0x0003);
}

TEST_CASE("session: matchesAppliedView is true when the applied set equals desired", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    SessionViewDto v;
    v.controllers = {view(0, proto::kControllerTypeXbox)};
    v.mouseControl.granted = false; // matches wantsMouseControl()==false
    REQUIRE(conn.matchesAppliedView(v));
}

TEST_CASE("session: matchesAppliedView is false on a type divergence", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypePlayStation, false, false);
    SessionViewDto v;
    v.controllers = {view(0, proto::kControllerTypeXbox)}; // server applied Xbox, we want DS
    REQUIRE_FALSE(conn.matchesAppliedView(v));
}

TEST_CASE("session: matchesAppliedView is false when a desired slot is missing server-side",
          "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    conn.attachSlot(QStringLiteral("slot-B"), proto::kControllerTypeXbox, false, false);
    SessionViewDto v;
    v.controllers = {view(0, proto::kControllerTypeXbox)}; // server missing slot 1
    REQUIRE_FALSE(conn.matchesAppliedView(v));
}

TEST_CASE("session: matchesAppliedView ignores an inactive applied slot", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    SessionViewDto v;
    v.controllers = {view(0, proto::kControllerTypeXbox, /*active=*/false)};
    REQUIRE_FALSE(conn.matchesAppliedView(v));
}

TEST_CASE("session: markConnecting moves Idle -> Linking", "[session]") {
    auto conn = makeConn();
    conn.markConnecting();
    REQUIRE(conn.state() == SessionState::Linking);
}

TEST_CASE("session: markConnected from Idle is rejected and leaves state Idle", "[session]") {
    auto conn = makeConn();
    // markConnected only takes effect from Linking; from Idle it must not promote.
    auto client = std::make_shared<SatelliteClient>();
    conn.markConnected(
        client, QStringLiteral("cid"), 1, false, [] {}, [](std::uint8_t) {}, [] {}, [] {});
    REQUIRE(conn.state() == SessionState::Idle);
}

TEST_CASE("session: markDisconnected from Idle is a no-op", "[session]") {
    auto conn = makeConn();
    conn.markDisconnected();
    REQUIRE(conn.state() == SessionState::Idle);
}

TEST_CASE("session: markStale parks the session in Stale (the 'Needs pairing' cue)", "[session]") {
    auto conn = makeConn();
    conn.markConnecting();
    conn.markStale();
    REQUIRE(conn.state() == SessionState::Stale);
}

TEST_CASE("session: updateServer replaces the server snapshot", "[session]") {
    auto conn = makeConn();
    conn.updateServer(server(QStringLiteral("10.0.0.99")));
    REQUIRE(conn.server().ip == QStringLiteral("10.0.0.99"));
}

TEST_CASE("session: adoptEpoch updates the reconcile reference", "[session]") {
    auto conn = makeConn();
    REQUIRE(conn.lastAppliedEpoch() == -1);
    conn.adoptEpoch(7);
    REQUIRE(conn.lastAppliedEpoch() == 7);
}

TEST_CASE("session: re-declaring a slot's type while idle emits no slotChanged", "[session]") {
    auto conn = makeConn();
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypeXbox, false, false);
    SignalCounter changedSpy(&conn, &WifiConnection::slotChanged);
    // While idle a re-declare only updates desired state; the next session PUT
    // carries it, so no per-controller sync is requested.
    conn.attachSlot(QStringLiteral("slot-A"), proto::kControllerTypePlayStation, false, false);
    REQUIRE(changedSpy.count == 0);
    REQUIRE(conn.descriptorFor(QStringLiteral("slot-A"))->type ==
            proto::kControllerTypePlayStation);
}

TEST_CASE("session: heartbeat cadence is the satellite-confirmed 2000 ms / 5-miss window",
          "[session][heartbeat]") {
    // Mirrors the satellite's HEARTBEAT_INTERVAL_SEC=2 / HEARTBEAT_MISS_MAX=5.
    REQUIRE(SatelliteClient::kHeartbeatIntervalMs == 2000);
    REQUIRE(SatelliteClient::kHeartbeatMissMax == 5);
    REQUIRE(SatelliteClient::kHeartbeatMissNotResponding == 2);
}
