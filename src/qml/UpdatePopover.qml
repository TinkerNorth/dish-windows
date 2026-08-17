// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The panel behind the update pill. It NEVER opens by itself and never takes
// focus: an update is an offer, not an interruption, and the app must stay
// usable with it on screen. Every action here is also reachable from Settings,
// so dismissing it costs the user nothing.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import "kit" as Kit

Popup {
    id: popover

    width: 320
    padding: Tokens.s7

    modal: false
    dim: false
    focus: false
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    readonly property bool busy: App.updatePhase === "downloading"
                                 || App.updatePhase === "verifying"
    readonly property bool ready: App.updatePhase === "ready"

    background: Rectangle {
        radius: Tokens.radiusDialog
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
    }

    contentItem: ColumnLayout {
        spacing: Tokens.s4

        Label {
            Layout.fillWidth: true
            text: popover.ready ? qsTr("Restart to update")
                 : popover.busy ? qsTr("Downloading Dish %1").arg(App.updateVersion)
                 : qsTr("Update available")
            color: Theme.onSurface
            font.pixelSize: Tokens.textHeading
            font.weight: Font.DemiBold
            wrapMode: Text.WordWrap
        }

        // The offer's facts: version and weight, in the mono voice the app uses
        // for every machine reading.
        Label {
            Layout.fillWidth: true
            visible: !popover.ready && !popover.busy
            text: qsTr("Dish %1 · %2").arg(App.updateVersion).arg(App.updateTotalText)
            color: Theme.muted
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textMeta
        }

        Label {
            Layout.fillWidth: true
            visible: popover.ready
            text: qsTr("Dish %1 is ready. Restart now, or it will be installed the next time Dish starts.")
                  .arg(App.updateVersion)
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: !popover.ready && !popover.busy && App.updatePortable
            text: qsTr("You're running the portable version. Get the new zip from the releases page.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            visible: !popover.ready && !popover.busy && !App.updatePortable
                     && App.updateMeteredDeferred
            text: qsTr("Waiting for an unmetered connection.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            wrapMode: Text.WordWrap
        }

        Kit.Callout {
            Layout.fillWidth: true
            visible: popover.ready && App.updateRequired
            tone: Kit.Callout.Warning
            text: qsTr("This version is no longer supported.")
        }

        Kit.DishProgressBar {
            Layout.fillWidth: true
            visible: popover.busy
            indeterminate: App.updateProgress < 0
            value: App.updateProgress
        }

        Label {
            Layout.fillWidth: true
            visible: popover.busy
            text: qsTr("%1 of %2").arg(App.updateReceivedText).arg(App.updateTotalText)
            color: Theme.muted
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textMeta
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s2
            spacing: Tokens.s4

            Kit.KitButton {
                visible: popover.ready
                text: qsTr("Restart now")
                onClicked: {
                    popover.close();
                    App.restartToApplyUpdate();
                }
            }
            Kit.OutlineButton {
                visible: popover.ready
                text: qsTr("Later")
                onClicked: popover.close()
            }

            // Portable copies never download: the releases page is the whole
            // update path for them.
            Kit.KitButton {
                visible: !popover.ready && !popover.busy && App.updatePortable
                text: qsTr("Open download page")
                onClicked: {
                    popover.close();
                    App.openReleaseNotes();
                }
            }
            Kit.KitButton {
                visible: !popover.ready && !popover.busy && !App.updatePortable
                text: qsTr("Download")
                onClicked: App.downloadUpdateNow()
            }

            Kit.OutlineButton {
                visible: !popover.busy
                text: qsTr("Release notes")
                onClicked: App.openReleaseNotes()
            }

            Item { Layout.fillWidth: true }
        }

        // Hidden while the update is required: there is nothing to skip to.
        Kit.OutlineButton {
            Layout.alignment: Qt.AlignLeft
            visible: !popover.ready && !popover.busy && !App.updateRequired
            size: Kit.DishButton.Small
            text: qsTr("Skip this version")
            onClicked: {
                popover.close();
                App.skipUpdate();
            }
        }
    }
}
