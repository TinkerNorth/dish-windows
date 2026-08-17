// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Failure stays plain: no scene, a diagnosis and a next step. The ErrorCode
// switch below is the single place an installer error becomes a sentence;
// the rollback outcome gets its own honest line, which the rollback-
// incomplete wording replaces (a reassurance that isn't true doesn't run).
// Try again re-runs with the same choices; an uninstall failure is not
// resumable, so its verb is Close.

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

    signal retryRequested()

    // The one ErrorCode -> sentence switch (house render-keys doctrine),
    // both modes.
    readonly property string errorText: {
        if (Setup.lastError === Setup.FileOpFailed) {
            if (Setup.lastErrorPath.length === 0)
                return qsTr("Something went wrong — see the log for details.");
            return face.uninstallMode
                   ? qsTr("Couldn’t remove %1 — is it open somewhere?").arg(Setup.lastErrorPath)
                   : qsTr("Couldn’t write to %1 — is another installer running?").arg(Setup.lastErrorPath);
        }
        if (Setup.lastError === Setup.RegistryFailed) {
            return face.uninstallMode ? qsTr("Couldn’t unregister the install from Windows.")
                                      : qsTr("Couldn’t register the install with Windows.");
        }
        if (Setup.lastError === Setup.ShortcutFailed) {
            const place = Setup.lastErrorPath.indexOf("Desktop") >= 0 ? qsTr("Desktop")
                                                                     : qsTr("Start Menu");
            return qsTr("Couldn’t create the %1 shortcut.").arg(place);
        }
        if (Setup.lastError === Setup.DiskFull)
            return qsTr("Not enough free space on this drive.");
        if (Setup.lastError === Setup.AppRunning)
            return qsTr("Dish is still running — close it and try again.");
        if (Setup.lastError === Setup.PayloadCorrupt)
            return qsTr("This installer’s files are damaged. Download it again, then retry.");
        if (Setup.lastError === Setup.RollbackIncomplete) {
            return face.uninstallMode
                   ? qsTr("Some files could not be removed — see the log.")
                   : qsTr("The install failed and some files could not be removed — see the log.");
        }
        return qsTr("Something went wrong — see the log for details.");
    }

    Accessible.name: heading.text + " — " + face.errorText

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
                    text: face.uninstallMode ? qsTr("Not removed") : qsTr("Not installed")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Label {
                    text: face.errorText
                    color: Theme.error
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Label {
                    // Only claimed when it is true: the install side rolled
                    // back, and the rollback itself reported complete.
                    visible: !face.uninstallMode
                             && Setup.lastError !== Setup.RollbackIncomplete
                    text: qsTr("Changes were undone; this PC is as it was.")
                    color: Theme.muted
                    font.pixelSize: Tokens.textMeta
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
            text: face.uninstallMode ? qsTr("Close") : qsTr("Try again")
            Layout.fillWidth: true
            onClicked: {
                if (face.uninstallMode)
                    Setup.quitSetup();
                else
                    face.retryRequested();
            }
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                text: qsTr("Open log")
                onClicked: Setup.openLogFile()
            }
            Text {
                visible: !face.uninstallMode
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Kit.LinkButton {
                visible: !face.uninstallMode
                text: qsTr("Close")
                onClicked: Setup.quitSetup()
            }
        }
    }
}
