// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The running-app gate as a face. The verb asks Dish to quit (WM_CLOSE +
// grace) and the face stays while the engine waits; only after that graceful
// attempt leaves blockers standing does the verb escalate to the destructive
// Force close, with a rescanning Try again beside the cancel — terminate is
// never the first offer. Esc = Cancel (the host cancels the run): a
// dismissed gate is a cancel, never a silent hang.
//
// The counted line is THE one %n string in the installer (spec D18):
// Bosnian needs its three plural forms and English both, filled in the .ts
// catalogues.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    // An install replaces files under the app; an uninstall cannot remove
    // files it holds open. Same gate, different honesty.
    property bool uninstallMode: false

    // Set once the graceful close has been asked of the engine; while the
    // blockers survive it, the face escalates.
    property bool closeAttempted: false
    readonly property bool graceFailed: face.closeAttempted && Setup.appRunning

    readonly property Item verbButton: verb

    signal cancelRequested()

    // Every arrival at the gate starts from the graceful offer.
    onVisibleChanged: {
        if (face.visible)
            face.closeAttempted = false;
    }

    Accessible.name: heading.text + " — " + countLine.text

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
                    text: qsTr("In the way")
                    mutedTone: true
                    horizontalAlignment: Text.AlignHCenter
                    Layout.fillWidth: true
                }
                Label {
                    id: heading
                    text: qsTr("Dish is running")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Label {
                    id: countLine
                    // THE counted string (D18): the one %n surface in the
                    // installer.
                    text: qsTr("%n running Dish window(s) must close before Setup continues.", "",
                               Setup.runningProcessCount)
                    color: Theme.muted
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    text: face.graceFailed
                          ? qsTr("Dish didn’t close. Save anything in flight, close it yourself, then try again.")
                          : face.uninstallMode
                            ? qsTr("Close it to continue — files it holds open can’t be removed.")
                            : qsTr("Close it to continue — replacing files under a running app breaks it.")
                    color: Theme.muted
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    // The processes themselves: data, not prose.
                    visible: Setup.runningProcessNames.length > 0
                    text: Setup.runningProcessNames.join(" · ")
                    color: Theme.mutedStrong
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        Kit.DishButton {
            id: verb
            size: Kit.DishButton.Large
            variant: face.graceFailed ? Kit.DishButton.Destructive : Kit.DishButton.Primary
            text: face.graceFailed ? qsTr("Force close") : qsTr("Close Dish and continue")
            Layout.fillWidth: true
            onClicked: {
                if (face.graceFailed) {
                    Setup.resolveBlockers(true);
                } else {
                    face.closeAttempted = true;
                    Setup.resolveBlockers(false);
                }
            }
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                visible: face.graceFailed
                text: qsTr("Try again")
                onClicked: Setup.rescanBlockers()
            }
            Text {
                visible: face.graceFailed
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
