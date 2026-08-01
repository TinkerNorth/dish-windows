// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SlotListModel — a thin QAbstractListModel adapter over the AppModel's slot
// list, so a QML ListView delegate can reproduce the Widgets SlotCard row. It
// holds NO business logic: every role mirrors a value the SlotCard already
// renders (name / bound status / capability chips / live-stat Hz). The data
// source is the same MainUiState slice the Widgets MainWindow reads; on each
// AppModel::stateChanged() the owner pushes the fresh state in via setState(),
// and this model emits the minimal dataChanged / rowsInserted / rowsRemoved.
//
// In dish_core (not the Quick target) so the unit tests link it without any
// Qml/Quick dependency — it needs only QAbstractListModel.

#pragma once

#include "Models/Models.h"
#include "composer/ConnectionsComposer.h"

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

#include <vector>

namespace dish::qml {

// The flattened view of one ControllerSlot the model exposes. Mirrors exactly
// what SlotCard::setSlot renders — the QML delegate reads these roles instead
// of re-deriving anything from the raw model. `boundLabel` / dot color / glyph
// are carried as data the delegate paints; the booleans gate chip visibility.
class SlotListModel : public QAbstractListModel {
    Q_OBJECT
    // A bare QAbstractListModel exposes no `count` to QML (only the *view* does),
    // so `model.count` in a binding reads undefined — which silently hides any
    // `count > 0` / `count === 0` gate. Expose it explicitly + reactively.
    Q_PROPERTY(int count READ count NOTIFY countChanged)
  public:
    // Roles mirror SlotCard's rendered fields one-to-one. Kept in sync with
    // roleNames(); the contract doc lists each.
    enum Roles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        BoundRole,             // bool: is the slot bound to a connection
        BoundConnectionIdRole, // QString: bound connection id ("" if unbound)
        BoundLabelRole,        // QString: "Bound to X" / "Unbound"
        LiveRole,              // bool: bound session is LinkState::Connected
        DotColorRole,          // QString: semantic token name ("success"/"warning"/"muted")
        UsbDirectRole,         // bool: synthetic USB-direct pad
        BluetoothRole,         // bool: pad is connected over Bluetooth
        RemappableRole,        // bool: raw joystick whose routing is user-remappable
        EmulateNameRole,       // QString: resolved type short name ("" hides the suffix)
        RegisteringRole,       // bool: attach in flight — card renders the busy state

        HasMotionRole,     // bool: hardware has a gyro/accelerometer
        HasLightbarRole,   // bool: hardware has an addressable RGB LED
        BatteryLevelRole,  // int: 0..100, or 255 (0xFF) when unknown
        BatteryStatusRole, // int: kBatteryStatus* wire constant
        BatteryKnownRole,  // bool: batteryLevel != 0xFF (chip is shown)

        GamepadHzRole,      // int: current/peak report Hz (per GamepadHzLiveRole)
        GamepadHzLiveRole,  // bool: gamepad Hz is a live measurement (USB-direct)
        GamepadHzShownRole, // bool: gamepad rate chip is visible
        MotionHzRole,       // int: IMU sample Hz (current or peak)
        MotionHzShownRole,  // bool: motion rate chip is visible
        PollHzRole,         // int: measured USB-direct poll Hz
        PollHzShownRole,    // bool: poll rate chip is visible

        // ── USB input-path state (the Standard/Direct control) ──────────────
        PathPhaseRole,       // QString: FSM phase token ("routed"/"claiming"/...)
        DesiredPathRole,     // QString: "standard"/"direct"/"auto"
        PathSupportedRole,   // bool: device is raw-HID-claimable (control shown)
        ClaimInProgressRole, // bool: phase == claiming (spinner, control disabled)
        DirectFailureRole,   // QString: last Direct-claim failure reason ("" if none)

        // ── Bound-satellite join (the Home signal-path row's right cell) ─────
        // The connection row this slot is bound to, joined by boundConnectionId
        // against the coordinator's derived rows (setConnectionRows). All empty
        // / zero for an unbound slot or a binding whose row has vanished — the
        // Home page renders the ghost "Bind…" action card then. The tokens are
        // the SAME vocabulary ConnectionListModel exposes, so the satellite
        // cell renders identically to a Connections-page row by construction.
        SatIpRole,             // QString: bound satellite ip ("" if unbound)
        SatLinkStateRole,      // QString token: "connected"/"unstable"/...
        SatChipRole,           // QString token: status-chip key
        SatDotColorRole,       // QString token: "success"/"primary"/"warning"/"muted"
        SatGlyphRole,          // QString token: "satelliteBase"/-Connected/-Off
        SatLatencyTextRole,    // QString: pre-formatted "~3.4 ms" ("" until sampled)
        SatLatencySamplesRole, // int: RTT samples in the window (gates the caption)
    };
    Q_ENUM(Roles)

    explicit SlotListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    int count() const { return static_cast<int>(slots_.size()); }
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Replace the model's contents with a fresh slot list. Diffs against the
    // current rows: appends/removes only the delta (rowsInserted/rowsRemoved)
    // and emits dataChanged for rows whose visible fields moved — so a quiet
    // 1 Hz telemetry tick that nudges one Hz value doesn't reset the ListView.
    // `available` is the connection set used to decide bind affordances; it is
    // carried so the delegate can gate a "bind" control, mirroring MainWindow's
    // "connections not bound elsewhere" filter.
    void setState(const QList<models::ControllerSlot>& slotList);

    // Replace the connection rows the Sat* join roles read through (pushed by
    // the owner on the coordinator's connectionsChanged, the same edge that
    // feeds ConnectionListModel). Emits dataChanged for the join roles only, so
    // the ~1 Hz latency tick refreshes the Home wire labels without churning
    // the slot-side bindings.
    void setConnectionRows(const std::vector<composer::ConnectionRow>& rows);

  signals:
    void countChanged();

  private:
    // The bound connection row for a slot, or nullptr when unbound / the id has
    // no row (the Home ghost-card state). A plain linear scan — both lists are
    // a handful of entries.
    const composer::ConnectionRow* rowForSlot(const models::ControllerSlot& slot) const;

    QList<models::ControllerSlot> slots_;
    std::vector<composer::ConnectionRow> connectionRows_;
};

} // namespace dish::qml
