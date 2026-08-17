// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Shown before the welcome when the probe finds an interrupted previous
// attempt. Clean up sweeps the stale journal (the probe's answer flipping is
// what moves the host on to the right welcome face); Not now proceeds
// without it.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property Item verbButton: verb

    signal skipRequested()

    Accessible.name: heading.text + " — " + sentence.text

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
                    text: qsTr("Recovery")
                    mutedTone: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                Label {
                    id: heading
                    text: qsTr("Finish cleaning up?")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Label {
                    id: sentence
                    text: qsTr("A previous setup attempt was interrupted and left recovery files behind. Clean them up before continuing.")
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
            id: verb
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Primary
            text: qsTr("Clean up")
            Layout.fillWidth: true
            onClicked: Setup.cleanStaleJournal()
        }

        RowLayout {
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                text: qsTr("Not now")
                onClicked: face.skipRequested()
            }
        }
    }
}
