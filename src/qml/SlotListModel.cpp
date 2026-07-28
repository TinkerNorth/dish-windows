// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "qml/SlotListModel.h"

#include "UI/SlotLiveStats.h"
#include "core/reducer/LatencyWindow.h"
#include "core/reducer/SlotPathFields.h"
#include "qml/RenderTokens.h"

namespace dish::qml {

namespace {

constexpr std::uint8_t kBatteryLevelUnknown = 0xFF;

// The stable phase token the QML path control switches on. Kept here (a
// view-model string concern) rather than on the Qt-free FSM enum; a test pins it
// indirectly through the role. Mirrors the android PathCard's phase strings.
QString pathPhaseToken(reducer::UsbPhase phase) {
    switch (phase) {
    case reducer::UsbPhase::Routed:
        return QStringLiteral("routed");
    case reducer::UsbPhase::Claiming:
        return QStringLiteral("claiming");
    case reducer::UsbPhase::Direct:
        return QStringLiteral("direct");
    case reducer::UsbPhase::AwaitingFramework:
        return QStringLiteral("awaitingFramework");
    case reducer::UsbPhase::RestoreStuck:
        return QStringLiteral("restoreStuck");
    case reducer::UsbPhase::NeedsReplug:
        return QStringLiteral("needsReplug");
    }
    return QStringLiteral("routed"); // unreachable; total switch
}

// The resolved desired path the toggle reads as selected. The stamped value is
// always the FSM's resolved PathChoice (Direct/Standard) — Auto resolves to one
// of these — so only those two strings appear; "auto" is an input to
// setSlotPath, never a reflected desired.
QString desiredPathToken(reducer::PathChoice choice) {
    return choice == reducer::PathChoice::Direct ? QStringLiteral("direct")
                                                 : QStringLiteral("standard");
}

// The last Direct-claim failure reason for the inline note. Empty when none.
QString directFailureToken(const std::optional<reducer::DirectClaimFailure>& failure) {
    if (!failure.has_value()) { return {}; }
    switch (*failure) {
    case reducer::DirectClaimFailure::PermissionDenied:
        return QStringLiteral("permissionDenied");
    case reducer::DirectClaimFailure::Busy:
        return QStringLiteral("busy");
    case reducer::DirectClaimFailure::InitFailed:
        return QStringLiteral("initFailed");
    case reducer::DirectClaimFailure::Dropped:
        return QStringLiteral("dropped");
    }
    return {}; // unreachable; total switch
}

// Semantic dot-color token a SlotCard paints: green only when the bound session
// is genuinely Connected, amber for any other bound-but-not-live state, muted
// when unbound. Mirrors SlotCard::setSlot's dot logic exactly.
QString dotColorToken(const models::ControllerSlot& s) {
    if (!s.boundStatus.has_value()) { return QStringLiteral("muted"); }
    return s.boundStatus->live == models::LinkState::Connected ? QStringLiteral("success")
                                                               : QStringLiteral("warning");
}

} // namespace

SlotListModel::SlotListModel(QObject* parent) : QAbstractListModel(parent) {}

int SlotListModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) { return 0; }
    return static_cast<int>(slots_.size());
}

QVariant SlotListModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() < 0 || index.row() >= slots_.size()) { return {}; }
    const auto& s = slots_.at(index.row());

    switch (role) {
    case IdRole:
        return s.id;
    case NameRole:
        return s.name;
    case BoundRole:
        return s.boundConnectionId.has_value();
    case BoundConnectionIdRole:
        return s.boundConnectionId.value_or(QString());
    case BoundLabelRole:
        return s.boundStatus.has_value() ? s.boundStatus->label : QString();
    case LiveRole:
        return s.boundStatus.has_value() && s.boundStatus->live == models::LinkState::Connected;
    case DotColorRole:
        return dotColorToken(s);
    case UsbDirectRole:
        return s.usbDirect;
    case RemappableRole:
        return s.remappable;
    case EmulateNameRole:
        return s.emulateName;
    case RegisteringRole:
        return s.registering;

    case HasMotionRole:
        return s.capabilities.hasMotion;
    case HasLightbarRole:
        return s.capabilities.hasLightbar;
    case BatteryLevelRole:
        return static_cast<int>(s.capabilities.batteryLevel);
    case BatteryStatusRole:
        return static_cast<int>(s.capabilities.batteryStatus);
    case BatteryKnownRole:
        return s.capabilities.batteryLevel != kBatteryLevelUnknown;

    // Live-stat roles delegate to the SAME pure SlotLiveStats mapper the widget
    // SlotCard uses, so the two UIs can never disagree about which chip shows.
    case GamepadHzRole: {
        const auto chip = ui::gamepadRateChip(s.liveRates, s.usbDirect);
        return chip.hz;
    }
    case GamepadHzLiveRole: {
        const auto chip = ui::gamepadRateChip(s.liveRates, s.usbDirect);
        return chip.kind == ui::RateChipKind::Live;
    }
    case GamepadHzShownRole: {
        const auto chip = ui::gamepadRateChip(s.liveRates, s.usbDirect);
        return chip.kind != ui::RateChipKind::Hidden;
    }
    case MotionHzRole: {
        const auto chip =
            s.capabilities.hasMotion ? ui::motionRateChip(s.liveRates) : ui::RateChip{};
        return chip.hz;
    }
    case MotionHzShownRole: {
        const auto chip =
            s.capabilities.hasMotion ? ui::motionRateChip(s.liveRates) : ui::RateChip{};
        return chip.kind != ui::RateChipKind::Hidden;
    }
    case PollHzRole: {
        const auto chip = ui::pollRateChip(s.liveRates, s.usbDirect);
        return chip.hz;
    }
    case PollHzShownRole: {
        const auto chip = ui::pollRateChip(s.liveRates, s.usbDirect);
        return chip.kind != ui::RateChipKind::Hidden;
    }

    // Bound-satellite join roles: read through the connection row matched by
    // boundConnectionId. The unbound / vanished-row fallbacks are the inert
    // empties (the Home page's ghost-card state).
    case SatIpRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? QString::fromStdString(row->ip) : QString();
    }
    case SatLinkStateRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? tokens::linkStateToken(row->live) : QString();
    }
    case SatChipRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? tokens::chipToken(row->chip) : QString();
    }
    case SatDotColorRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? tokens::dotToken(row->dotColor) : QString();
    }
    case SatGlyphRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? tokens::glyphToken(row->glyph) : QString();
    }
    case SatLatencyTextRole: {
        // Same formatter + same samples gate as ConnectionListModel, so the
        // Home wire label and the Connections row can never disagree.
        const auto* row = rowForSlot(s);
        return row != nullptr && row->latencySamples > 0
                   ? QString::fromStdString(reducer::formatLatencyMs(row->latencyOneWayMs))
                   : QString();
    }
    case SatLatencySamplesRole: {
        const auto* row = rowForSlot(s);
        return row != nullptr ? row->latencySamples : 0;
    }

    case PathPhaseRole:
        return pathPhaseToken(s.pathPhase);
    case DesiredPathRole:
        return desiredPathToken(s.desiredPath);
    case PathSupportedRole:
        return s.pathSupported;
    case ClaimInProgressRole:
        // The single derived "switching" state (pure reducer): true through the
        // whole path-switch lifecycle — the transitional FSM phase AND the device-
        // form/telemetry settle (Direct until its synthetic's poll rate is
        // measured, Standard until the synthetic is gone). Drives the toggle's
        // spinner + disabled state with no arbitrary timer.
        return reducer::slotPathSwitching(s.pathPhase, s.desiredPath, s.usbDirect,
                                          s.liveRates.directPollHz, s.directFailure.has_value());
    case DirectFailureRole:
        return directFailureToken(s.directFailure);
    default:
        return {};
    }
}

QHash<int, QByteArray> SlotListModel::roleNames() const {
    return {
        {IdRole, "slotId"},
        {NameRole, "name"},
        {BoundRole, "bound"},
        {BoundConnectionIdRole, "boundConnectionId"},
        {BoundLabelRole, "boundLabel"},
        {LiveRole, "live"},
        {DotColorRole, "dotColor"},
        {UsbDirectRole, "usbDirect"},
        {RemappableRole, "remappable"},
        {EmulateNameRole, "emulateName"},
        {RegisteringRole, "registering"},
        {HasMotionRole, "hasMotion"},
        {HasLightbarRole, "hasLightbar"},
        {BatteryLevelRole, "batteryLevel"},
        {BatteryStatusRole, "batteryStatus"},
        {BatteryKnownRole, "batteryKnown"},
        {GamepadHzRole, "gamepadHz"},
        {GamepadHzLiveRole, "gamepadHzLive"},
        {GamepadHzShownRole, "gamepadHzShown"},
        {MotionHzRole, "motionHz"},
        {MotionHzShownRole, "motionHzShown"},
        {PollHzRole, "pollHz"},
        {PollHzShownRole, "pollHzShown"},
        {PathPhaseRole, "pathPhase"},
        {DesiredPathRole, "desiredPath"},
        {PathSupportedRole, "pathSupported"},
        {ClaimInProgressRole, "claimInProgress"},
        {DirectFailureRole, "directFailure"},
        {SatIpRole, "satIp"},
        {SatLinkStateRole, "satLinkState"},
        {SatChipRole, "satChip"},
        {SatDotColorRole, "satDotColor"},
        {SatGlyphRole, "satGlyph"},
        {SatLatencyTextRole, "satLatencyText"},
        {SatLatencySamplesRole, "satLatencySamples"},
    };
}

void SlotListModel::setState(const QList<models::ControllerSlot>& slotList) {
    const int oldCount = static_cast<int>(slots_.size());
    const int newCount = static_cast<int>(slotList.size());

    // Append/remove only the tail delta so a steady slot list with a moved Hz
    // value never resets the ListView (no full reset). Slot identity is by
    // position here: the AppModel emits a stable-ordered list (virtual slot
    // first, then attached pads in a stable order), and a slot's per-row content
    // is patched in place via dataChanged below. A genuine add/remove shifts the
    // tail, which the count delta covers.
    if (newCount > oldCount) {
        beginInsertRows({}, oldCount, newCount - 1);
        slots_ = slotList;
        endInsertRows();
    } else if (newCount < oldCount) {
        beginRemoveRows({}, newCount, oldCount - 1);
        slots_ = slotList;
        endRemoveRows();
    } else {
        slots_ = slotList;
    }

    // Patch the surviving rows: emit one dataChanged spanning [0, min) so a
    // binding re-reads any role whose value moved. Cheap for the handful of
    // slots a machine ever has, and correct without a per-role diff.
    const int common = newCount < oldCount ? newCount : oldCount;
    if (common > 0) { emit dataChanged(index(0), index(common - 1)); }

    if (oldCount != newCount) { emit countChanged(); }
}

void SlotListModel::setConnectionRows(const std::vector<composer::ConnectionRow>& rows) {
    connectionRows_ = rows;
    // Only the join roles read through the rows; scoping the dataChanged to
    // them keeps the ~1 Hz latency tick from re-evaluating every slot binding.
    if (!slots_.isEmpty()) {
        static const QList<int> kJoinRoles{
            SatIpRole,    SatLinkStateRole,   SatChipRole,          SatDotColorRole,
            SatGlyphRole, SatLatencyTextRole, SatLatencySamplesRole};
        emit dataChanged(index(0), index(static_cast<int>(slots_.size()) - 1), kJoinRoles);
    }
}

const composer::ConnectionRow* SlotListModel::rowForSlot(const models::ControllerSlot& slot) const {
    if (!slot.boundConnectionId.has_value()) { return nullptr; }
    const std::string id = slot.boundConnectionId->toStdString();
    for (const auto& row : connectionRows_) {
        if (row.id == id) { return &row; }
    }
    return nullptr;
}

} // namespace dish::qml
