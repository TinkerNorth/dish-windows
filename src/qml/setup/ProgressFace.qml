// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Work in flight: the verb's slot becomes the progress bar, byte-accurate
// from manifest sizes, and the app mark above pulses — the only motion
// besides the bar. Cancel is live only while Copying; the atomic commit /
// finalize / rollback windows refuse it, and an uninstall never offers it
// (removal is not resumable). Step detail (shortcuts, registry) lives in the
// log, not on screen. Elevation waits here too, bar indeterminate.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    property bool uninstallMode: false

    readonly property bool awaitingElevation: Setup.phase === Setup.AwaitingElevation
    readonly property bool copying: Setup.phase === Setup.Copying
    readonly property bool cancellable: !face.uninstallMode && face.copying

    readonly property Item verbButton: face.cancellable ? cancelLink : null

    signal cancelRequested()

    readonly property string label: {
        if (face.awaitingElevation)
            return qsTr("Waiting for Windows approval…");
        if (Setup.phase === Setup.RollingBack)
            return qsTr("Undoing changes…");
        if (Setup.phase === Setup.Committing || Setup.phase === Setup.Finalizing)
            return qsTr("Finishing up…");
        const pct = Math.round(Setup.progress * 100);
        return face.uninstallMode ? qsTr("Removing… %1%").arg(pct)
                                  : qsTr("Installing… %1%").arg(pct);
    }

    Accessible.name: face.label

    ColumnLayout {
        anchors.fill: parent
        spacing: Tokens.s6

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Kit.AppMark {
                anchors.centerIn: parent
                width: 56
                height: 56
                busy: face.visible
            }
        }

        Kit.DishProgressBar {
            thick: true
            indeterminate: face.awaitingElevation
            value: Setup.progress
            Accessible.role: Accessible.ProgressBar
            Accessible.name: face.uninstallMode ? qsTr("Removal progress")
                                                : qsTr("Install progress")
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Text {
                text: face.label
                color: Theme.mutedStrong
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            Text {
                visible: face.copying && Setup.fileCount > 0
                text: qsTr("file %1 of %2").arg(Setup.fileIndex).arg(Setup.fileCount)
                color: Theme.mutedStrong
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
            }
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                id: cancelLink
                visible: !face.uninstallMode
                enabled: face.cancellable
                text: qsTr("Cancel")
                onClicked: face.cancelRequested()
            }
            Text {
                // The helper's after-exit cleanup, stated once, quietly.
                visible: face.uninstallMode
                text: qsTr("Cleans itself up after this window closes")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
