// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QFrame>
#include <QString>

class QLabel;
class QPushButton;

namespace dish::ui {

// One row in the slots list. Mirrors the Mac SlotCard / Android slot row.
class SlotCard : public QFrame {
    Q_OBJECT
  public:
    explicit SlotCard(QWidget* parent = nullptr);

    void setSlot(const models::ControllerSlot& slot,
                 const QList<models::ConnectionSummary>& available);

  signals:
    void bindRequested(const QString& slotId, const QString& connectionId);
    void unbindRequested(const QString& slotId);
    // The player tapped "Emulate" on a bound slot: open the catalog-driven type
    // picker for it (Workstream 2c). Only emitted while the slot is bound.
    void emulateRequested(const QString& slotId);

  private:
    void onBindClicked();

    QLabel* nameLabel_;
    QLabel* boundLabel_;
    // v6 brand satellite glyph from `:/brand/satellite[-state].svg` —
    // each slot binds to a satellite server, so the silhouette mirrors
    // the ConnectionsDialog row this slot routes to. State tracks the
    // bound connection's LinkState via setBrandIcon() so the silhouette
    // and the colour dot reinforce the same signal.
    QLabel* glyph_;
    QLabel* dot_;
    QPushButton* bindButton_;
    // "Emulate" affordance — opens the catalog-driven type picker. Shown only
    // when the slot is bound (there is a satellite to emulate a pad on).
    QPushButton* emulateButton_;
    // Capability chip: motion (gyro/accelerometer) availability for this pad.
    // Always visible — its text/style/tooltip switch between the "available"
    // and "not available" states so the absence of motion is explicit, never
    // just an absent indicator.
    QLabel* motionChip_;
    // Capability chip: addressable RGB lightbar (DualSense / DualShock 4).
    // Unlike the motion chip this is shown ONLY when the pad has an LED — a
    // missing lightbar is the common case (Xbox / generic pads) and needs no
    // "not available" callout.
    QLabel* lightbarChip_;
    // Battery chip: charge for this pad — the controller's own for a wireless
    // pad, the host machine's for a wired/unknown one. Hidden until the first
    // battery sample arrives (level 0xFF / unknown).
    QLabel* batteryChip_;

    models::ControllerSlot slot_;
    QList<models::ConnectionSummary> available_;
};

} // namespace dish::ui
