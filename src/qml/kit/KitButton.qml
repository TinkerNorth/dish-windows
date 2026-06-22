// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The PRIMARY action button: a filled, primary-tinted pill on the Basic style.
// Pages use this for the one main action of a view (Scan, Pair, Bind…). For a
// quieter action use OutlineButton.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Button {
    id: control

    font.pixelSize: 13
    font.bold: true
    implicitHeight: 34
    leftPadding: 16
    rightPadding: 16

    // The Dish design system drops the whole control to 0.4 alpha when disabled
    // (matches the Widgets `applyDisabledOpacityEffect` / ds-components rule).
    opacity: control.enabled ? 1.0 : 0.4

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.background        // on-primary: dark text on the cyan fill
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        color: control.down ? Qt.darker(Theme.primary, 1.15)
             : control.hovered ? Qt.lighter(Theme.primary, 1.08)
             : Theme.primary
    }
}
