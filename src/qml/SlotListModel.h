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

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>

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
        BoundRole,            // bool: is the slot bound to a connection
        BoundConnectionIdRole, // QString: bound connection id ("" if unbound)
        BoundLabelRole,       // QString: "Bound to X" / "Unbound"
        LiveRole,             // bool: bound session is LinkState::Connected
        DotColorRole,         // QString: semantic token name ("success"/"warning"/"muted")
        UsbDirectRole,        // bool: synthetic USB-direct pad

        HasMotionRole,        // bool: hardware has a gyro/accelerometer
        HasLightbarRole,      // bool: hardware has an addressable RGB LED
        BatteryLevelRole,     // int: 0..100, or 255 (0xFF) when unknown
        BatteryStatusRole,    // int: kBatteryStatus* wire constant
        BatteryKnownRole,     // bool: batteryLevel != 0xFF (chip is shown)

        GamepadHzRole,        // int: current/peak report Hz (per GamepadHzLiveRole)
        GamepadHzLiveRole,    // bool: gamepad Hz is a live measurement (USB-direct)
        GamepadHzShownRole,   // bool: gamepad rate chip is visible
        MotionHzRole,         // int: IMU sample Hz (current or peak)
        MotionHzShownRole,    // bool: motion rate chip is visible
        PollHzRole,           // int: measured USB-direct poll Hz
        PollHzShownRole,      // bool: poll rate chip is visible

        // ── USB input-path state (the Standard/Direct control) ──────────────
        PathPhaseRole,        // QString: FSM phase token ("routed"/"claiming"/...)
        DesiredPathRole,      // QString: "standard"/"direct"/"auto"
        PathSupportedRole,    // bool: device is raw-HID-claimable (control shown)
        ClaimInProgressRole,  // bool: phase == claiming (spinner, control disabled)
        DirectFailureRole,    // QString: last Direct-claim failure reason ("" if none)
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

  signals:
    void countChanged();

  private:
    QList<models::ControllerSlot> slots_;
};

} // namespace dish::qml
