// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A declined UAC prompt: diagnosis plus a next step, never a dialog loop.
// "Install for just me instead" turns the buried banner advice into an
// action — it flips scope to per-user (folder rewrites to that scope's
// default) and re-runs. Hidden on upgrades and uninstalls, whose scope is
// the record's, not a choice.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    property bool uninstallMode: false

    readonly property Item verbButton: verb

    signal cancelRequested()

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

                Label {
                    id: heading
                    text: qsTr("Windows didn’t approve")
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
                    text: face.uninstallMode
                          ? qsTr("Removing Dish for everyone on this PC needs administrator approval.")
                          : qsTr("Installing for everyone on this PC needs administrator approval.")
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
            text: qsTr("Try again")
            Layout.fillWidth: true
            onClicked: {
                if (face.uninstallMode)
                    Setup.beginUninstall();
                else
                    Setup.beginInstall();
            }
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                id: perUserLink
                visible: !face.uninstallMode && !Setup.existingDetected
                text: qsTr("Install for just me instead")
                onClicked: {
                    Setup.scope = Setup.PerUser;
                    Setup.installDir = Setup.defaultDirFor(Setup.PerUser);
                    Setup.beginInstall();
                }
            }
            Text {
                visible: perUserLink.visible
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Kit.LinkButton {
                text: qsTr("Cancel")
                onClicked: face.cancelRequested()
            }
        }
    }
}
