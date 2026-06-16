// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Connections destination — the Qt Quick port of the Widgets
// ConnectionsDialog. FOUND (App.discoveredServers()) + REMEMBERED
// (App.connectionModel), a Scan action, per-row Connect/Pair/Forget, and the
// pairing ContentDialog. All behavior forwards to App (QML_CONTRACT.md); this
// file holds zero business logic.

// Bound component behavior so delegates resolve outer ids (page, the dialog)
// statically — keeps binding resolution static and the linter quiet (matches
// AppShell.qml). `App` stays unqualified: it is a runtime context property the
// linter cannot resolve, the same accepted limitation as the Theme QColor
// warnings noted in QML_UI_KIT.md.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Connections")

    // discoveredServers() is a plain method (no NOTIFY), so a binding off it does
    // not re-evaluate when a scan lands. App.busy / a stateChanged tick drives the
    // header; we re-pull the list on the model's stateChanged so FOUND refreshes
    // without polling. `discovered` is the single re-pulled snapshot the FOUND
    // Repeater and the pairing dialog index against.
    property var discovered: App.discoveredServers()
    function refreshDiscovered() { page.discovered = App.discoveredServers(); }

    // Localized chip text for a ConnectionListModel `chip` token. Kept in QML
    // because it is pure presentation (the C++ vends the token, not the copy).
    function chipText(token) {
        switch (token) {
        case "found":        return qsTr("Found");
        case "needsPairing": return qsTr("Needs pairing");
        case "offline":      return qsTr("Offline");
        case "ready":        return qsTr("Ready");
        case "connecting":   return qsTr("Connecting…");
        case "online":       return qsTr("Online");
        case "unstable":     return qsTr("Unstable");
        default:             return token;
        }
    }

    Connections {
        target: App
        // A scan completing / a server appearing folds into stateChanged; re-pull
        // the FOUND snapshot so the Repeater below tracks it.
        function onStateChanged() { page.refreshDiscovered(); }
    }

    // ---- FOUND -------------------------------------------------------------

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Kit.BrandGlyph { Layout.preferredWidth: 18; Layout.preferredHeight: 18; glyph: "satellite" }
        Kit.SectionHeader { label: qsTr("Found"); Layout.fillWidth: true }

        // KitButton has no built-in busy state; overlay a BusyIndicator and lean
        // on `enabled` for the disabled-while-scanning look.
        Kit.KitButton {
            id: scanButton
            text: App.isScanning() ? qsTr("Scanning…") : qsTr("Scan")
            enabled: !App.isScanning()
            onClicked: App.startDiscovery()

            BusyIndicator {
                anchors.centerIn: parent
                running: App.isScanning()
                visible: running
                implicitWidth: 20
                implicitHeight: 20
            }
        }
    }

    // Empty-state for FOUND.
    Kit.Card {
        Layout.fillWidth: true
        visible: page.discovered.length === 0
        contentItem: Label {
            text: App.isScanning() ? qsTr("Scanning for satellites…")
                                   : qsTr("No satellites found yet. Tap Scan to look for one.")
            color: Theme.muted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    // One Card per discovered server. The Repeater lays them out inside the
    // Page's default Column, so each Card is a sibling stacking row.
    Repeater {
        model: page.discovered
        delegate: Kit.Card {
            id: foundCard
            required property int index
            required property var modelData

            width: parent ? parent.width : implicitWidth

            contentItem: RowLayout {
                spacing: 12

                Kit.BrandGlyph { Layout.preferredWidth: 24; Layout.preferredHeight: 24; glyph: "satellite" }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: foundCard.modelData.name.length > 0 ? foundCard.modelData.name
                                                                  : foundCard.modelData.ip
                        color: Theme.onSurface
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: foundCard.modelData.ip
                        color: Theme.muted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                Kit.OutlineButton {
                    text: qsTr("Connect")
                    onClicked: App.connectByIndex(foundCard.index)
                }

                Kit.KitButton {
                    id: pairButton
                    text: App.isPairingInFlight(foundCard.modelData.id) ? qsTr("Pairing…")
                                                                        : qsTr("Pair")
                    enabled: !App.isPairingInFlight(foundCard.modelData.id)
                    onClicked: pairDialog.openFor(foundCard.index,
                                                  foundCard.modelData.id,
                                                  foundCard.modelData.name.length > 0
                                                      ? foundCard.modelData.name
                                                      : foundCard.modelData.ip)

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: App.isPairingInFlight(foundCard.modelData.id)
                        visible: running
                        implicitWidth: 20
                        implicitHeight: 20
                    }
                }
            }
        }
    }

    // ---- REMEMBERED --------------------------------------------------------

    RowLayout {
        Layout.fillWidth: true
        spacing: 8
        Kit.BrandGlyph { Layout.preferredWidth: 18; Layout.preferredHeight: 18; glyph: "satellite" }
        Kit.SectionHeader { label: qsTr("Remembered"); Layout.fillWidth: true }
    }

    // Empty-state for REMEMBERED. ConnectionListModel (a QAbstractListModel)
    // exposes no `count` property to QML, so key the empty-state off the
    // Repeater's own item count.
    Kit.Card {
        Layout.fillWidth: true
        visible: rememberedRepeater.count === 0
        contentItem: Label {
            text: qsTr("No remembered connections yet. Pair a satellite above to add one.")
            color: Theme.muted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    Repeater {
        id: rememberedRepeater
        model: App.connectionModel
        delegate: Kit.Card {
            id: rememberedCard

            // ConnectionListModel roles (QML_CONTRACT.md §3).
            required property string connectionId
            required property string label
            required property string ip
            required property int udpPort
            required property string chip
            required property string dotColor
            required property string glyph

            width: parent ? parent.width : implicitWidth

            contentItem: RowLayout {
                spacing: 12

                Kit.BrandGlyph {
                    Layout.preferredWidth: 24; Layout.preferredHeight: 24
                    glyph: glyphForToken(rememberedCard.glyph)
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: rememberedCard.label
                        color: Theme.onSurface
                        font.pixelSize: 14
                        font.bold: true
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: qsTr("%1 • UDP %2").arg(rememberedCard.ip).arg(rememberedCard.udpPort)
                        color: Theme.muted
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                RowLayout {
                    spacing: 6
                    Kit.StatusDot { token: rememberedCard.dotColor; Layout.alignment: Qt.AlignVCenter }
                    Label {
                        text: page.chipText(rememberedCard.chip)
                        color: Theme.muted
                        font.pixelSize: 12
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                Kit.OutlineButton {
                    text: qsTr("Forget")
                    onClicked: App.forgetConnection(rememberedCard.connectionId)
                }
            }
        }
    }

    // ---- PAIRING dialog ----------------------------------------------------

    Kit.ContentDialog {
        id: pairDialog
        heading: qsTr("Pair with %1").arg(pairDialog.serverName)
        acceptText: App.isPairingInFlight(pairDialog.serverId) ? qsTr("Pairing…") : qsTr("Pair")
        rejectText: qsTr("Cancel")
        // Enable accept only on a full 6-digit PIN and while no request is in
        // flight for this server.
        acceptEnabled: pinField.text.length === 6 && !App.isPairingInFlight(pairDialog.serverId)

        // The discovered server this dialog targets — set by openFor() before
        // open() so the contract calls (pairWithPin index, isPairingInFlight id)
        // address the right row.
        property int discoveredIndex: -1
        property string serverId: ""
        property string serverName: ""

        function openFor(index, id, name) {
            pairDialog.discoveredIndex = index;
            pairDialog.serverId = id;
            pairDialog.serverName = name;
            pinField.clear();
            // Drop any parked one-shot pairing trigger before showing the sheet
            // (contract: clearPairingTarget before opening to avoid re-entry).
            App.clearPairingTarget();
            pairDialog.open();
        }

        contentColumn.children: [
            Label {
                text: qsTr("Enter the 6-digit PIN displayed on %1").arg(pairDialog.serverName)
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: pinField
                placeholderText: qsTr("6-digit PIN")
                maximumLength: 6
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0 }
                // Disabled mid-request: a new PIN would race the in-flight call.
                enabled: !App.isPairingInFlight(pairDialog.serverId)
                Layout.fillWidth: true
            },
            // Inline error surface (App.errorMessage is one-shot; the page owns
            // showing it). Sits in the body, above the footer.
            Label {
                id: errorBanner
                visible: false
                color: Theme.error
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        ]

        // The dialog does NOT auto-close on accept (kit convention). Submit and
        // wait: close on the success edge (in-flight clears with no error), stay
        // open on errorMessage so the user can retry the same field.
        property bool submitted: false

        onAccepted: {
            pairDialog.submitted = true;
            App.pairWithPin(pairDialog.discoveredIndex, pinField.text);
        }
        onRejected: pinField.clear()

        Connections {
            target: App
            enabled: pairDialog.visible

            // Keep the dialog OPEN on error and surface the message; arm a guard
            // so the next in-flight-clear edge does not mistake the failed attempt
            // for a success and auto-dismiss.
            function onErrorMessage(message) {
                pairDialog.submitted = false;
                errorBanner.text = message;
                errorBanner.visible = message.length > 0;
            }

            // On any state move, if a submission is no longer in flight and no
            // error vetoed it, the pairing succeeded — close the sheet.
            function onStateChanged() {
                if (pairDialog.submitted && !App.isPairingInFlight(pairDialog.serverId)) {
                    pairDialog.submitted = false;
                    pairDialog.close();
                }
            }
        }

        onClosed: {
            errorBanner.visible = false;
            errorBanner.text = "";
            pairDialog.submitted = false;
        }
    }
}
