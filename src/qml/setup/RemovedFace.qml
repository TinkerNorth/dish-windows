// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// At rest. When the settings were kept, the face names exactly where they
// stayed — what is left behind is a documented choice, never a surprise —
// and the Satellite-hosts reminder lives here, where forgetting this PC is
// actionable next. The helper finishes the uninstaller's own cleanup after
// this window closes; the Removing face already said so once.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property Item verbButton: verb

    Accessible.name: heading.text + " — " + satelliteLine.text

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

                Kit.AppMark {
                    // Layout-managed: size via the preferred pair, never
                    // bare width/height.
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: 56
                    Layout.preferredHeight: 56
                }
                Label {
                    id: heading
                    text: qsTr("Removed")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Label {
                    visible: !Setup.wantPurgeUserData
                    // The location is data, not prose: the literal shell path
                    // the settings actually live under, locale-independent.
                    text: qsTr("Your settings stayed at %1 — delete that folder too if you want nothing left.").arg("%LOCALAPPDATA%\\Dish")
                    color: Theme.muted
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    id: satelliteLine
                    text: qsTr("Satellite hosts remember this PC until you forget it there.")
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
            text: qsTr("Close")
            Layout.fillWidth: true
            onClicked: Setup.quitSetup()
        }
    }
}
