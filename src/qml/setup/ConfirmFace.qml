// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The face-swap confirm (downgrade, stop-installing): no dialog, the window
// itself asks. House rule, kept from ConfirmDialog: the SAFE answer is the
// primary and holds focus, so a stray Enter is never the destructive one;
// the destructive verb wears the error outline and comes second in the tab
// chain. Esc routes to safeChosen via the host.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit

Item {
    id: face

    property string eyebrowText: ""
    property string heading: ""
    property string sentence: ""
    property string safeText: ""
    property string destructiveText: ""

    readonly property Item verbButton: safeVerb

    signal safeChosen()
    signal destructiveChosen()

    Accessible.name: face.heading + " — " + face.sentence

    ColumnLayout {
        anchors.fill: parent
        spacing: Tokens.s6

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Tokens.s5

                Kit.Eyebrow {
                    text: face.eyebrowText
                    color: Theme.warning
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                Label {
                    text: face.heading
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Label {
                    text: face.sentence
                    color: Theme.muted
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        Kit.DishButton {
            id: safeVerb
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Primary
            text: face.safeText
            Layout.fillWidth: true
            onClicked: face.safeChosen()
        }
        Kit.DishButton {
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Destructive
            text: face.destructiveText
            Layout.fillWidth: true
            onClicked: face.destructiveChosen()
        }
    }
}
