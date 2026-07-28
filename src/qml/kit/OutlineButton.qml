// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The SECONDARY / outlined button: transparent fill, themed outline, primary
// text — the quieter sibling of KitButton (mirrors the Widgets
// `outlinedButtonQss()`). Use for secondary actions (Forget, Cancel, Manage).

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

    opacity: control.enabled ? 1.0 : Tokens.disabledOpacity

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.primary : Theme.muted
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Tokens.radiusButton
        // Transparent so a Mica surface shows through behind a button placed on
        // bare chrome; only the canonical accent washes fill on hover/press.
        color: control.down ? Theme.primaryPress
             : control.hovered ? Theme.primaryHover
             : "transparent"
        // The ds outlined button borders in the ACCENT, not the hairline — the
        // border is what makes it read as an action.
        border.width: 1
        border.color: control.enabled ? Theme.primary : Theme.muted
    }
}
