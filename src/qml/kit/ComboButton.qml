// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Compact dropdown chooser (design FCombo): the current value + a ▾ caret in a
// recessed pill; clicking opens an in-scene menu of options. Emits
// picked(option) — the selected value streams back through `value`.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Rectangle {
    id: control

    property var options: []
    property string value: ""

    signal picked(string option)

    implicitWidth: content.implicitWidth + 24
    implicitHeight: content.implicitHeight + 12
    radius: Tokens.radiusButton
    color: Theme.surfaceDim
    border.width: 1
    border.color: Theme.outline

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Tokens.s5

        Text {
            text: control.value
            font.pixelSize: Tokens.textSummary
            color: Theme.onSurface
        }
        Text {
            text: "▾"
            font.family: Tokens.monoFamily
            font.pixelSize: 9
            color: Theme.muted
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: menu.open()
    }

    Menu {
        id: menu
        y: control.height + 2

        background: Rectangle {
            implicitWidth: Math.max(control.width, 140)
            color: Theme.surface
            border.width: 1
            border.color: Theme.outline
            radius: Tokens.radiusButton
        }

        Repeater {
            model: control.options
            delegate: MenuItem {
                required property string modelData
                text: modelData

                contentItem: Text {
                    text: parent.modelData
                    font.pixelSize: Tokens.textSummary
                    color: parent.modelData === control.value ? Theme.primary : Theme.onSurface
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: parent.highlighted ? Theme.primaryHover : "transparent"
                    radius: 4
                }
                onTriggered: control.picked(modelData)
            }
        }
    }
}
