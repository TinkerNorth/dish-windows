// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Segmented single-choice control (design FSeg / the slot card's FPathSeg):
// options in a recessed pill, the selected one filled with the accent. `small`
// is the dense slot-card variant. Emits picked(option) — the caller applies
// the change and the selected state streams back through `value`, so the
// control never holds its own truth.

import QtQuick
import Dish.Chrome

Rectangle {
    id: control

    property var options: []
    property string value: ""
    property bool small: false
    // Dimmed + inert while an async apply is in flight (claiming a pad).
    property bool busy: false

    signal picked(string option)

    implicitWidth: row.implicitWidth + 2 * row.anchors.margins
    implicitHeight: row.implicitHeight + 2 * row.anchors.margins
    radius: small ? Tokens.radiusButton : Tokens.radiusCard
    color: Theme.surfaceDim
    border.width: 1
    border.color: Theme.outline
    opacity: busy ? 0.45 : 1.0

    Row {
        id: row
        anchors.centerIn: parent
        anchors.margins: control.small ? 2 : 3
        spacing: control.small ? 3 : 4

        Repeater {
            model: control.options
            delegate: Rectangle {
                required property string modelData
                readonly property bool selected: modelData === control.value

                width: label.implicitWidth + (control.small ? 18 : 32)
                height: label.implicitHeight + (control.small ? 5 : 10)
                radius: control.small ? 4 : 5
                color: selected ? Theme.primary : "transparent"

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: modelData
                    font.pixelSize: control.small ? 10.5 : Tokens.textBase
                    font.weight: parent.selected ? Font.DemiBold : Font.Normal
                    color: parent.selected ? Theme.onPrimary : Theme.muted
                }

                MouseArea {
                    anchors.fill: parent
                    enabled: !control.busy
                    cursorShape: Qt.PointingHandCursor
                    onClicked: control.picked(parent.modelData)
                }
            }
        }
    }
}
