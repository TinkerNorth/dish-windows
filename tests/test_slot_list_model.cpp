// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotListModel — the QAbstractListModel adapter that lets a QML ListView
// delegate reproduce the Widgets SlotCard. These tests pin the ADAPTER's
// contract: roleNames coverage, row-count tracking, data() per role (mirroring
// SlotCard's rendered fields + the shared SlotLiveStats mapper), and the
// minimal change signals (rowsInserted/rowsRemoved on a count delta,
// dataChanged on an in-place patch). No QML rendering is exercised — only the
// mapping/signalling. The model is driven directly by a hand-built MainUiState
// slot list (the same value the AppModel already computes), since the adapter's
// input IS that slice; it owns no store of its own.

#include "Models/Models.h"
#include "composer/ConnectionsComposer.h"
#include "core/reducer/DirectClaimFailure.h"
#include "core/reducer/LatencyWindow.h"
#include "core/reducer/PathChoice.h"
#include "core/reducer/UsbPathMachine.h"
#include "qml/SlotListModel.h"

#include <catch2/catch_test_macros.hpp>

#include <QObject>
#include <QVariant>

using dish::qml::SlotListModel;
namespace m = dish::models;

namespace {

// A tiny QSignalSpy stand-in — DishTests links Catch2, not Qt6::Test, so we
// count rows-changed signals with a plain lambda connect and record the row
// span of the last insert/remove. Mirrors what we'd assert with QSignalSpy.
struct RowSpy {
    int inserts = 0;
    int removes = 0;
    int changes = 0;
    int firstRow = -1;
    int lastRow = -1;

    explicit RowSpy(QAbstractItemModel* model) {
        QObject::connect(model, &QAbstractItemModel::rowsInserted,
                         [this](const QModelIndex&, int first, int last) {
                             ++inserts;
                             firstRow = first;
                             lastRow = last;
                         });
        QObject::connect(model, &QAbstractItemModel::rowsRemoved,
                         [this](const QModelIndex&, int first, int last) {
                             ++removes;
                             firstRow = first;
                             lastRow = last;
                         });
        QObject::connect(
            model, &QAbstractItemModel::dataChanged,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>&) { ++changes; });
    }
};

// A bound, motion+lightbar, USB-direct pad with measured rates — exercises the
// "everything on" arm of every role.
m::ControllerSlot richSlot() {
    m::ControllerSlot s;
    s.id = QStringLiteral("slot-1");
    s.name = QStringLiteral("DualSense");
    s.boundConnectionId = QStringLiteral("conn-1");
    m::ConnectionSummary cs;
    cs.id = QStringLiteral("conn-1");
    cs.label = QStringLiteral("Living Room");
    cs.live = m::LinkState::Connected;
    s.boundStatus = cs;
    s.capabilities.hasMotion = true;
    s.capabilities.hasLightbar = true;
    s.capabilities.batteryLevel = 80;
    s.capabilities.batteryStatus = 1; // discharging
    s.usbDirect = true;
    s.liveRates.gamepadHz = 250;
    s.liveRates.gamepadPeakHz = 250;
    s.liveRates.motionHz = 200;
    s.liveRates.directPollHz = 1000;
    return s;
}

// A minimal, unbound, no-motion SDL pad with no measured rates — the "all off"
// arm: chips hidden, battery unknown, dot muted.
m::ControllerSlot plainSlot(const QString& id = QStringLiteral("slot-0")) {
    m::ControllerSlot s;
    s.id = id;
    s.name = QStringLiteral("Xbox Pad");
    return s; // batteryLevel defaults to 0xFF, no caps, no rates
}

QVariant roleOf(const SlotListModel& model, int row, int role) {
    return model.data(model.index(row, 0), role);
}

} // namespace

TEST_CASE("SlotListModel: roleNames covers every Roles enumerator", "[slotmodel][roles]") {
    SlotListModel model;
    const auto names = model.roleNames();
    // One entry per declared role (Id..VerifiedModel). If a role is added
    // without a name, this count drifts and the test flags it.
    REQUIRE(names.size() == 38);
    REQUIRE(names.value(SlotListModel::IdRole) == QByteArray("slotId"));
    REQUIRE(names.value(SlotListModel::NameRole) == QByteArray("name"));
    REQUIRE(names.value(SlotListModel::BluetoothRole) == QByteArray("bluetooth"));
    REQUIRE(names.value(SlotListModel::RemappableRole) == QByteArray("remappable"));
    REQUIRE(names.value(SlotListModel::EmulateNameRole) == QByteArray("emulateName"));
    REQUIRE(names.value(SlotListModel::RegisteringRole) == QByteArray("registering"));
    REQUIRE(names.value(SlotListModel::GamepadHzShownRole) == QByteArray("gamepadHzShown"));
    REQUIRE(names.value(SlotListModel::PollHzShownRole) == QByteArray("pollHzShown"));
    REQUIRE(names.value(SlotListModel::PathPhaseRole) == QByteArray("pathPhase"));
    REQUIRE(names.value(SlotListModel::DesiredPathRole) == QByteArray("desiredPath"));
    REQUIRE(names.value(SlotListModel::PathSupportedRole) == QByteArray("pathSupported"));
    REQUIRE(names.value(SlotListModel::ClaimInProgressRole) == QByteArray("claimInProgress"));
    REQUIRE(names.value(SlotListModel::DirectFailureRole) == QByteArray("directFailure"));
    REQUIRE(names.value(SlotListModel::SatIpRole) == QByteArray("satIp"));
    REQUIRE(names.value(SlotListModel::SatLinkStateRole) == QByteArray("satLinkState"));
    REQUIRE(names.value(SlotListModel::SatChipRole) == QByteArray("satChip"));
    REQUIRE(names.value(SlotListModel::SatDotColorRole) == QByteArray("satDotColor"));
    REQUIRE(names.value(SlotListModel::SatGlyphRole) == QByteArray("satGlyph"));
    REQUIRE(names.value(SlotListModel::SatLatencyTextRole) == QByteArray("satLatencyText"));
    REQUIRE(names.value(SlotListModel::SatLatencySamplesRole) == QByteArray("satLatencySamples"));
    REQUIRE(names.value(SlotListModel::HasTouchpadRole) == QByteArray("hasTouchpad"));
    REQUIRE(names.value(SlotListModel::VerifiedModelRole) == QByteArray("verifiedModel"));
    // No duplicate role names (each maps a distinct delegate property).
    QSet<QByteArray> unique;
    for (const auto& n : names) { unique.insert(n); }
    REQUIRE(unique.size() == names.size());
}

TEST_CASE("SlotListModel: rowCount tracks the pushed slot list", "[slotmodel][rows]") {
    SlotListModel model;
    REQUIRE(model.rowCount() == 0);
    model.setState({plainSlot(), richSlot()});
    REQUIRE(model.rowCount() == 2);
    model.setState({plainSlot()});
    REQUIRE(model.rowCount() == 1);
    model.setState({});
    REQUIRE(model.rowCount() == 0);
}

TEST_CASE("SlotListModel: data maps a bound rich pad's roles", "[slotmodel][data]") {
    SlotListModel model;
    model.setState({richSlot()});

    REQUIRE(roleOf(model, 0, SlotListModel::IdRole).toString() == "slot-1");
    REQUIRE(roleOf(model, 0, SlotListModel::NameRole).toString() == "DualSense");
    REQUIRE(roleOf(model, 0, SlotListModel::BoundRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::BoundConnectionIdRole).toString() == "conn-1");
    REQUIRE(roleOf(model, 0, SlotListModel::BoundLabelRole).toString() == "Living Room");
    REQUIRE(roleOf(model, 0, SlotListModel::LiveRole).toBool());
    // Connected -> success dot (mirrors SlotCard).
    REQUIRE(roleOf(model, 0, SlotListModel::DotColorRole).toString() == "success");
    REQUIRE(roleOf(model, 0, SlotListModel::UsbDirectRole).toBool());

    REQUIRE(roleOf(model, 0, SlotListModel::HasMotionRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::HasLightbarRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::BatteryLevelRole).toInt() == 80);
    REQUIRE(roleOf(model, 0, SlotListModel::BatteryStatusRole).toInt() == 1);
    REQUIRE(roleOf(model, 0, SlotListModel::BatteryKnownRole).toBool());

    // USB-direct + a live gamepadHz -> Live chip shown at the live value.
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzRole).toInt() == 250);
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzLiveRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzShownRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::MotionHzRole).toInt() == 200);
    REQUIRE(roleOf(model, 0, SlotListModel::MotionHzShownRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::PollHzRole).toInt() == 1000);
    REQUIRE(roleOf(model, 0, SlotListModel::PollHzShownRole).toBool());
}

TEST_CASE("SlotListModel: data maps an unbound plain pad's off-state roles", "[slotmodel][data]") {
    SlotListModel model;
    model.setState({plainSlot()});

    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::BoundRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::BoundConnectionIdRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::BoundLabelRole).toString().isEmpty());
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::LiveRole).toBool());
    // Unbound -> muted dot.
    REQUIRE(roleOf(model, 0, SlotListModel::DotColorRole).toString() == "muted");
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::HasMotionRole).toBool());
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::HasLightbarRole).toBool());
    // 0xFF level -> battery chip not known/shown.
    REQUIRE(roleOf(model, 0, SlotListModel::BatteryLevelRole).toInt() == 255);
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::BatteryKnownRole).toBool());
    // No measurements -> every rate chip hidden.
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::GamepadHzShownRole).toBool());
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::MotionHzShownRole).toBool());
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::PollHzShownRole).toBool());
}

TEST_CASE("SlotListModel: hasTouchpad reflects the pad's touch surface", "[slotmodel][data]") {
    // The Input layer of the capability solver gates BOTH the Touchpad row and
    // the Mouse row on this, so a pad without a touch surface can never be
    // offered mouse routing.
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::HasTouchpadRole).toBool());

    auto s = richSlot();
    s.capabilities.hasTouchpad = true;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::HasTouchpadRole).toBool());
}

TEST_CASE("SlotListModel: verifiedModel reflects the known-layout flag", "[slotmodel][data]") {
    // Drives the Direct option card's "Layout guessed" chip: false means the
    // raw-HID fast lane would be guessing this model's report layout.
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::VerifiedModelRole).toBool());

    auto s = richSlot();
    s.verifiedModel = true;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::VerifiedModelRole).toBool());
}

TEST_CASE("SlotListModel: remappable defaults off and reflects the slot flag",
          "[slotmodel][data]") {
    // Only a raw-joystick-backed SDL slot is remappable; a default slot (synthetic
    // / game controller / virtual) is not, so the page entry stays hidden for it.
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::RemappableRole).toBool());

    auto s = plainSlot();
    s.remappable = true;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::RemappableRole).toBool());
}

TEST_CASE("SlotListModel: bluetooth defaults off and reflects the slot flag", "[slotmodel][data]") {
    // Only a pad the bridge classified as Bluetooth-attached reads true; the
    // default (wired / synthetic / unknown-path) slot keeps the wired
    // presentation — glyph family and transport chip stay satellite/USB.
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::BluetoothRole).toBool());

    auto s = plainSlot();
    s.bluetooth = true;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::BluetoothRole).toBool());
}

TEST_CASE("SlotListModel: a bound-but-not-connected slot shows the warning dot",
          "[slotmodel][data]") {
    SlotListModel model;
    auto s = richSlot();
    s.boundStatus->live = m::LinkState::Connecting;
    model.setState({s});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::LiveRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::DotColorRole).toString() == "warning");
}

TEST_CASE("SlotListModel: a non-direct pad reports its gamepad rate as a hidden-unless-peak chip",
          "[slotmodel][rates]") {
    SlotListModel model;
    auto s = plainSlot();
    s.usbDirect = false;
    s.liveRates.gamepadHz = 120;   // live value present but NOT direct
    s.liveRates.gamepadPeakHz = 0; // no peak
    model.setState({s});
    // Not direct + no peak -> hidden (mirrors gamepadRateChip).
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::GamepadHzShownRole).toBool());

    s.liveRates.gamepadPeakHz = 144;
    model.setState({s});
    // Peak present -> shown as a peak (not live).
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzShownRole).toBool());
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::GamepadHzLiveRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzRole).toInt() == 144);
}

TEST_CASE("SlotListModel: appending a slot emits rowsInserted only for the delta",
          "[slotmodel][signals]") {
    SlotListModel model;
    model.setState({plainSlot(QStringLiteral("a"))});

    RowSpy spy(&model);
    model.setState({plainSlot(QStringLiteral("a")), plainSlot(QStringLiteral("b"))});

    REQUIRE(spy.inserts == 1);
    REQUIRE(spy.removes == 0);
    // [first, last] == [1, 1] — only the new tail row.
    REQUIRE(spy.firstRow == 1);
    REQUIRE(spy.lastRow == 1);
    REQUIRE(model.rowCount() == 2);
}

TEST_CASE("SlotListModel: removing a slot emits rowsRemoved only for the delta",
          "[slotmodel][signals]") {
    SlotListModel model;
    model.setState({plainSlot(QStringLiteral("a")), plainSlot(QStringLiteral("b"))});

    RowSpy spy(&model);
    model.setState({plainSlot(QStringLiteral("a"))});

    REQUIRE(spy.removes == 1);
    REQUIRE(spy.inserts == 0);
    REQUIRE(spy.firstRow == 1);
    REQUIRE(spy.lastRow == 1);
    REQUIRE(model.rowCount() == 1);
}

TEST_CASE("SlotListModel: a same-count telemetry tick emits dataChanged, not a reset",
          "[slotmodel][signals]") {
    SlotListModel model;
    model.setState({richSlot()});

    RowSpy spy(&model);

    auto s = richSlot();
    s.liveRates.gamepadHz = 500; // a Hz moved; same slot count
    model.setState({s});

    REQUIRE(spy.inserts == 0);
    REQUIRE(spy.removes == 0);
    REQUIRE(spy.changes == 1);
    // The patched value is visible on re-read.
    REQUIRE(roleOf(model, 0, SlotListModel::GamepadHzRole).toInt() == 500);
}

TEST_CASE("SlotListModel: countChanged fires on a row-count delta, not a same-count patch",
          "[slotmodel][signals]") {
    // The reactive `count` property (the page's empty-state + bind gates read it)
    // must re-emit only when the slot count actually moves — a quiet telemetry
    // tick (same count, new Hz) must NOT, or QML re-evaluates the gates needlessly.
    SlotListModel model;
    model.setState({richSlot()});

    int countEmissions = 0;
    QObject::connect(&model, &SlotListModel::countChanged, [&countEmissions] { ++countEmissions; });

    // Same count, a Hz moved -> dataChanged only, no countChanged.
    auto s = richSlot();
    s.liveRates.gamepadHz = 500;
    model.setState({s});
    REQUIRE(countEmissions == 0);

    // Slot added -> countChanged.
    model.setState({s, plainSlot(QStringLiteral("b"))});
    REQUIRE(countEmissions == 1);
    REQUIRE(model.count() == 2);

    // Slot removed -> countChanged again.
    model.setState({s});
    REQUIRE(countEmissions == 2);
    REQUIRE(model.count() == 1);
}

TEST_CASE("SlotListModel: out-of-range index returns an invalid variant", "[slotmodel][data]") {
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(model.data(model.index(5, 0), SlotListModel::NameRole).isValid());
}

namespace r = dish::reducer;

TEST_CASE("SlotListModel: path roles default to the inert unsupported state", "[slotmodel][path]") {
    // A plain slot carries no USB path entry -> the control is hidden and the
    // tokens read their defaults.
    SlotListModel model;
    model.setState({plainSlot()});
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::PathSupportedRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::PathPhaseRole).toString() == "routed");
    REQUIRE(roleOf(model, 0, SlotListModel::DesiredPathRole).toString() == "standard");
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::ClaimInProgressRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::DirectFailureRole).toString().isEmpty());
}

TEST_CASE("SlotListModel: a supported Direct slot exposes the path roles as tokens",
          "[slotmodel][path]") {
    SlotListModel model;
    auto s = richSlot();
    s.pathSupported = true;
    s.pathPhase = r::UsbPhase::Direct;
    s.desiredPath = r::PathChoice::Direct;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::PathSupportedRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::PathPhaseRole).toString() == "direct");
    REQUIRE(roleOf(model, 0, SlotListModel::DesiredPathRole).toString() == "direct");
    // Direct (not Claiming) -> no in-flight spinner.
    REQUIRE_FALSE(roleOf(model, 0, SlotListModel::ClaimInProgressRole).toBool());
}

TEST_CASE("SlotListModel: claimInProgress is true exactly in the Claiming phase",
          "[slotmodel][path]") {
    SlotListModel model;
    auto s = plainSlot();
    s.pathSupported = true;
    s.pathPhase = r::UsbPhase::Claiming;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::ClaimInProgressRole).toBool());
    REQUIRE(roleOf(model, 0, SlotListModel::PathPhaseRole).toString() == "claiming");
}

TEST_CASE(
    "SlotListModel: a Direct failure surfaces as a token; needsReplug/restoreStuck phases map",
    "[slotmodel][path]") {
    SlotListModel model;
    auto s = plainSlot();
    s.pathSupported = true;
    s.pathPhase = r::UsbPhase::Routed;
    s.directFailure = r::DirectClaimFailure::Busy;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::DirectFailureRole).toString() == "busy");

    s.directFailure.reset();
    s.pathPhase = r::UsbPhase::NeedsReplug;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::PathPhaseRole).toString() == "needsReplug");
    REQUIRE(roleOf(model, 0, SlotListModel::DirectFailureRole).toString().isEmpty());

    s.pathPhase = r::UsbPhase::RestoreStuck;
    model.setState({s});
    REQUIRE(roleOf(model, 0, SlotListModel::PathPhaseRole).toString() == "restoreStuck");
}

// ── Bound-satellite join (the Home signal-path right cell) ───────────────────

namespace {

// A connected connection row for the join, carrying the render keys + latency
// exactly as the ConnectionsComposer derives them.
dish::composer::ConnectionRow connectedRow(const std::string& id = "conn-1") {
    dish::composer::ConnectionRow row;
    row.id = id;
    row.label = "Living Room";
    row.live = r::UiLinkState::Connected;
    row.ip = "192.168.1.24";
    row.udpPort = 47811;
    row.glyph = r::ConnectionGlyph::SatelliteConnected;
    row.dotColor = r::DotColor::Success;
    row.chip = r::StatusChipKey::Online;
    row.latencyOneWayMs = 3.4;
    row.latencySamples = 64;
    return row;
}

} // namespace

TEST_CASE("SlotListModel: a bound slot joins its connection row's render tokens",
          "[slotmodel][satjoin]") {
    SlotListModel model;
    model.setState({richSlot()}); // bound to "conn-1"
    model.setConnectionRows({connectedRow()});

    REQUIRE(roleOf(model, 0, SlotListModel::SatIpRole).toString() == "192.168.1.24");
    REQUIRE(roleOf(model, 0, SlotListModel::SatLinkStateRole).toString() == "connected");
    REQUIRE(roleOf(model, 0, SlotListModel::SatChipRole).toString() == "online");
    REQUIRE(roleOf(model, 0, SlotListModel::SatDotColorRole).toString() == "success");
    REQUIRE(roleOf(model, 0, SlotListModel::SatGlyphRole).toString() == "satelliteConnected");
    // Same formatter + samples gate as ConnectionListModel — the Home wire
    // label and the Connections row can never disagree.
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencyTextRole).toString() ==
            QString::fromStdString(dish::reducer::formatLatencyMs(3.4)));
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencySamplesRole).toInt() == 64);
}

TEST_CASE("SlotListModel: an unbound slot's join roles are the inert empties",
          "[slotmodel][satjoin]") {
    SlotListModel model;
    model.setState({plainSlot()});
    model.setConnectionRows({connectedRow()}); // a row exists, but nothing binds it

    REQUIRE(roleOf(model, 0, SlotListModel::SatIpRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatLinkStateRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatChipRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatDotColorRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatGlyphRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencyTextRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencySamplesRole).toInt() == 0);
}

TEST_CASE("SlotListModel: a binding whose row has vanished degrades to the empties",
          "[slotmodel][satjoin]") {
    // A forget can drop the row while the slot still carries the (stale)
    // binding for a beat — the Home cell must render the ghost, not garbage.
    SlotListModel model;
    model.setState({richSlot()}); // bound to "conn-1"
    model.setConnectionRows({connectedRow("conn-OTHER")});

    REQUIRE(roleOf(model, 0, SlotListModel::SatIpRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatChipRole).toString().isEmpty());
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencySamplesRole).toInt() == 0);
}

TEST_CASE("SlotListModel: the latency text stays empty until the window has samples",
          "[slotmodel][satjoin]") {
    SlotListModel model;
    model.setState({richSlot()});
    auto row = connectedRow();
    row.latencySamples = 0; // value carried but unseeded
    model.setConnectionRows({row});
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencyTextRole).toString().isEmpty());
}

TEST_CASE("SlotListModel: setConnectionRows patches in place - no reset, no countChanged",
          "[slotmodel][satjoin][signals]") {
    SlotListModel model;
    model.setState({richSlot()});

    RowSpy spy(&model);
    int countEmissions = 0;
    QObject::connect(&model, &SlotListModel::countChanged, [&countEmissions] { ++countEmissions; });

    model.setConnectionRows({connectedRow()});

    REQUIRE(spy.inserts == 0);
    REQUIRE(spy.removes == 0);
    REQUIRE(spy.changes == 1);
    REQUIRE(countEmissions == 0);
    // The joined value is visible on re-read (the ~1 Hz latency tick path).
    REQUIRE(roleOf(model, 0, SlotListModel::SatLatencySamplesRole).toInt() == 64);
}

TEST_CASE("SlotListModel: setConnectionRows with no slots emits nothing", "[slotmodel][satjoin]") {
    SlotListModel model;
    RowSpy spy(&model);
    model.setConnectionRows({connectedRow()});
    REQUIRE(spy.changes == 0);
}
