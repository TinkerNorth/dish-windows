// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A labeled toggle: a leading text label (+ optional sub-text) on the left and a
// themed Switch on the right. Use it for Settings rows and the wizard's feel
// rows. `checked` is two-way bindable; `toggled(checked)` fires on user
// interaction.
//
// DISABLED DIMS THE CONTROL, NEVER THE REASON. Setting `enabled: false` fades
// only the switch — the label and the description keep full opacity, because
// "Motion — this pad has no gyro" is exactly the text the user has to read to
// understand why the control is dead. Nesting a 0.55 row inside a 0.55 chip is
// how a reason lands at 30 % and stops being information.

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

    spacing: Tokens.s6

    ColumnLayout {
        Layout.fillWidth: true
        spacing: Tokens.s1

        Label {
            id: title
            color: Theme.onSurface
            font.pixelSize: Tokens.textBase
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: row.description
            visible: row.description.length > 0
            // A reason is information: a contrast-tuned colour, not a fade.
            color: Theme.mutedStrong
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }

    Switch {
        id: sw

        // The Basic style folds the indicator's width into the contentItem's
        // leftPadding, so replacing the contentItem (below) drops it out of the
        // implicit size and the control collapses to its padding — the track
        // then paints past the row and is clipped by the card. Size off the
        // indicator instead.
        implicitWidth: sw.implicitIndicatorWidth + sw.leftPadding + sw.rightPadding
        implicitHeight: sw.implicitIndicatorHeight + sw.topPadding + sw.bottomPadding

        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        // The 0.55 disabled rule is legal only on an AbstractButton — and this
        // is the only part of the row that is one.
        opacity: sw.enabled ? 1.0 : Tokens.disabledOpacity

        Accessible.role: Accessible.CheckBox
        Accessible.name: title.text
        Accessible.description: row.description
        Accessible.checked: sw.checked

        onToggled: row.toggled(sw.checked)

        indicator: Item {
            implicitWidth: 38
            implicitHeight: 22
            // A replaced indicator is not positioned by the style either.
            x: sw.leftPadding
            y: sw.topPadding + (sw.availableHeight - height) / 2

            Rectangle {
                id: track
                anchors.fill: parent
                radius: height / 2
                color: sw.checked ? Theme.primary : Theme.surfaceDim
                border.width: 1
                border.color: !sw.enabled ? Theme.disabledFg
                            : sw.checked ? "transparent"
                            : sw.hovered ? Theme.primary
                            : Theme.outline

                Behavior on color {
                    enabled: !Tokens.reducedMotion
                    ColorAnimation { duration: Tokens.durFast }
                }

                Rectangle {
                    id: knob
                    width: 16
                    height: 16
                    radius: height / 2
                    y: 3
                    x: sw.checked ? parent.width - width - 3 : 3
                    color: !sw.enabled ? Theme.disabledFg
                         : sw.checked ? Theme.onPrimary : Theme.muted

                    Behavior on x {
                        enabled: !Tokens.reducedMotion
                        NumberAnimation { duration: Tokens.durFast }
                    }
                }
            }

            // The global focus ring: 2px outside the track, on visualFocus only.
            Rectangle {
                anchors.fill: track
                anchors.margins: -2
                radius: (track.height + 4) / 2
                visible: sw.visualFocus
                color: "transparent"
                border.width: 2
                border.color: Theme.focusRing
            }
        }

        // Suppress the default text slot; the label lives in the layout.
        contentItem: Item {}
    }
}
