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

    font.pixelSize: 13
    font.bold: true
    implicitHeight: 34
    leftPadding: 16
    rightPadding: 16

    opacity: control.enabled ? 1.0 : 0.4

    contentItem: Text {
        text: control.text
        font: control.font
        color: Theme.primary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        // Transparent so a Mica surface shows through behind a button placed on
        // bare chrome; only a subtle hover/press wash fills in.
        color: control.down ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
             : control.hovered ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.10)
             : "transparent"
        border.width: 1
        border.color: Theme.outline
    }
}
