// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Capability pill (design-system CapabilityChip): `present` renders the filled
// accent-tinted pill (Gyro, Lightbar, Battery); otherwise a dimmed outlined
// "not available" pill. `low` swaps the accent tint for the amber warning
// tokens (low battery). Promoted to the kit so the slot cards, the states
// board and the deadzone rows all draw the one pill.

import QtQuick
import Dish.Chrome

Rectangle {
    id: chip

    property string text: ""
    property bool present: true
    property bool low: false

    implicitWidth: label.implicitWidth + 14
    implicitHeight: label.implicitHeight + 4
    radius: Tokens.radiusChip
    color: low ? Theme.warningFill : present ? Theme.primaryFill : "transparent"
    border.width: 1
    border.color: low || present ? "transparent" : Theme.muted

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        font.pixelSize: Tokens.textChip
        font.weight: chip.low ? Font.DemiBold : Font.Medium
        color: chip.low ? Theme.warning : chip.present ? Theme.primary : Theme.muted
    }
}
