// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Labeled percentage slider (design FSlider): label + mono % readout over a
// thin recessed track with an accent fill and a round handle.
//
// TWO signals, because a dead-zone drag has two different consumers:
//   moved(value)     — every step of the gesture. Push this into the LIVE
//                      processor so the pad responds under the user's thumb.
//   committed(value) — release (or the end of a keyboard adjustment). PERSIST
//                      here only; a drag emits a value per pixel and each one
//                      would otherwise be a repository write.
// Keyboard adjustment commits on key release, so ←/→ persists exactly like a
// drag does.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Column {
    id: control

    property string label: ""
    property int value: 0
    property int maxValue: 30

    // Live, per-step. Never persist from here.
    signal moved(int value)
    // Terminal, on release. Persist from here.
    signal committed(int value)

    spacing: Tokens.s4

    Item {
        width: parent.width
        height: Math.max(nameText.implicitHeight, pctText.implicitHeight)

        Text {
            id: nameText
            anchors.left: parent.left
            anchors.baseline: pctText.baseline
            text: control.label
            font.pixelSize: Tokens.textSummary
            color: control.enabled ? Theme.onSurface : Theme.disabledFg
        }
        Text {
            id: pctText
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            // Always the slider's own value: a drag or an arrow key breaks the
            // inbound binding, and the readout must follow the handle, not the
            // last value the page happened to push back.
            text: Math.round(slider.value) + "%"
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
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        opacity: slider.enabled ? 1.0 : Tokens.disabledOpacity

        Accessible.role: Accessible.Slider
        Accessible.name: control.label

        onMoved: control.moved(Math.round(slider.value))
        onPressedChanged: if (!slider.pressed) control.committed(Math.round(slider.value))

        // Qt's Slider handles the arrow / page / home / end keys itself and
        // emits moved(); it has no notion of a gesture ending, so the commit is
        // hung off the key release.
        Keys.onReleased: event => {
            if (event.key === Qt.Key_Left || event.key === Qt.Key_Right
                    || event.key === Qt.Key_Up || event.key === Qt.Key_Down
                    || event.key === Qt.Key_PageUp || event.key === Qt.Key_PageDown
                    || event.key === Qt.Key_Home || event.key === Qt.Key_End)
                control.committed(Math.round(slider.value));
        }

        background: Rectangle {
            x: slider.leftPadding
            y: slider.topPadding + slider.availableHeight / 2 - height / 2
            width: slider.availableWidth
            height: 4
            radius: Tokens.radiusBar
            color: Theme.surfaceDim
            border.width: 1
            border.color: slider.enabled ? Theme.outline : Theme.disabledFg

            Rectangle {
                width: slider.visualPosition * parent.width
                height: parent.height
                radius: Tokens.radiusBar
                color: slider.enabled ? Theme.primary : Theme.disabledFg
            }
        }

        handle: Item {
            id: grip
            x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - grip.width)
            y: slider.topPadding + slider.availableHeight / 2 - grip.height / 2
            // The handle grows under the pointer so a 12px target reads as a
            // grab affordance before the drag starts.
            width: (slider.pressed || slider.hovered || slider.visualFocus) ? 14 : 12
            height: grip.width

            Behavior on width {
                enabled: !Tokens.reducedMotion
                NumberAnimation { duration: Tokens.durFast }
            }

            Rectangle {
                anchors.fill: parent
                radius: grip.width / 2
                color: slider.enabled ? Theme.primary : Theme.disabledFg
            }

            // The global focus ring: 2px outside the handle, on visualFocus only.
            Rectangle {
                anchors.fill: parent
                anchors.margins: -2
                radius: (grip.width + 4) / 2
                visible: slider.visualFocus
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
            }
        }
    }
}
