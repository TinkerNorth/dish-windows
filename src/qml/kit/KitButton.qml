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

    font.pixelSize: Tokens.textBase
    font.weight: Font.Medium
    implicitHeight: 30
    leftPadding: Tokens.s6
    rightPadding: Tokens.s6

    // The Dish design system drops the whole control to 0.4 alpha when disabled
    // (matches the Widgets `applyDisabledOpacityEffect` / ds-components rule).
    opacity: control.enabled ? 1.0 : Tokens.disabledOpacity

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.onPrimary : Theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Tokens.radiusButton
        // Hover/press darken to the pressed accent (the ds Button spec); the
        // disabled fill recedes so the muted text stays legible at 0.4.
        color: !control.enabled ? Theme.surfaceDim
             : control.down || control.hovered ? Theme.primaryDark
             : Theme.primary
    }
}
