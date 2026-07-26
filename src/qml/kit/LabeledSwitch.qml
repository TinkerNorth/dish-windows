// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A labeled toggle: a leading text label (+ optional sub-text) on the left and a
// themed Switch on the right. Use it for Settings rows. `checked` is two-way
// bindable; `toggled(checked)` fires on user interaction.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

RowLayout {
    id: row

    property alias label: title.text
    property string description: ""
    property alias checked: sw.checked
    signal toggled(bool checked)

    spacing: 12

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 2
        Label {
            id: title
            color: Theme.onSurface
            font.pixelSize: 13
        }
        Label {
            text: row.description
            visible: row.description.length > 0
            color: Theme.muted
            font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Switch {
        id: sw
        onToggled: row.toggled(checked)

        indicator: Rectangle {
            implicitWidth: 38
            implicitHeight: 22
            radius: height / 2
            color: sw.checked ? Theme.primary : Theme.surfaceDim
            border.width: 1
            border.color: sw.checked ? "transparent" : Theme.outline

            Rectangle {
                width: 16
                height: 16
                radius: 8
                y: 3
                x: sw.checked ? parent.width - width - 3 : 3
                color: sw.checked ? Theme.onPrimary : Theme.muted
                Behavior on x { NumberAnimation { duration: 120 } }
            }
        }
        // Suppress the default text slot; the label lives in the layout.
        contentItem: Item {}
    }
}
