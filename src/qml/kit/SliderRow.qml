// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Labeled percentage slider (design FSlider): label + mono % readout over a
// thin recessed track with an accent fill and a 12px round handle. Emits
// committed(value) on release so a drag doesn't spam persistence.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Column {
    id: control

    property string label: ""
    property int value: 0
    property int maxValue: 30

    signal committed(int value)

    spacing: 7

    Item {
        width: parent.width
        height: Math.max(nameText.implicitHeight, pctText.implicitHeight)

        Text {
            id: nameText
            anchors.left: parent.left
            anchors.baseline: pctText.baseline
            text: control.label
            font.pixelSize: Tokens.textSummary
            color: Theme.onSurface
        }
        Text {
            id: pctText
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            text: (slider.pressed ? Math.round(slider.value) : control.value) + "%"
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textMeta
            color: Theme.muted
        }
    }

    Slider {
        id: slider
        width: parent.width
        from: 0
        to: control.maxValue
        stepSize: 1
        value: control.value
        onPressedChanged: if (!pressed) control.committed(Math.round(value))

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: Tokens.radiusBar
            color: Theme.surfaceDim
            border.width: 1
            border.color: Theme.outline

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: Tokens.radiusBar
                color: Theme.primary
            }
        }

        handle: Rectangle {
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: 12
            height: 12
            radius: 6
            color: Theme.primary
        }
    }
}
