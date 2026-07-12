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

    // FOUND + scan flag are now REACTIVE properties on App (App.discoveredServers
    // / App.scanning, NOTIFY discoveredChanged / scanningChanged). Bind them
    // directly so the list and the Scan button stream as a scan progresses — no
    // hand-cached snapshot, no manual refresh handler (the old non-reactive
    // invokables only updated on page recreation).

    // isPairingInFlight() is a plain invokable (no NOTIFY), so a binding that
    // reads it never re-evaluates on its own when a pair starts/finishes. Bump
    // this counter on every App.stateChanged and reference it alongside the
    // invokable in those bindings to force a re-read on each state move.
    property int pairTick: 0

    Connections {
        target: App
        function onStateChanged() { page.pairTick++; }
    }

    // Scan on open (android #125 parity): entering the destination surfaces
    // reachable satellites without an extra Scan tap, and the same pass
    // re-homes a remembered satellite whose box moved to a new address. The
    // shell recreates the page on each rail visit, so this fires per entry;
    // startDiscovery() is guarded manager-side, so a scan already in flight
    // is never double-triggered.
    Component.onCompleted: if (!App.scanning) App.startDiscovery()

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
            text: App.scanning ? qsTr("Scanning…") : qsTr("Scan")
            enabled: !App.scanning
            onClicked: App.startDiscovery()

            BusyIndicator {
                anchors.centerIn: parent
                running: App.scanning
                visible: running
                implicitWidth: 20
                implicitHeight: 20
            }
        }
    }

    // Empty-state for FOUND.
    Kit.Card {
        Layout.fillWidth: true
        visible: App.discoveredServers.length === 0
        contentItem: Label {
            text: App.scanning ? qsTr("Scanning for satellites…")
                               : qsTr("No satellites found yet. Tap Scan to look for one.")
            color: Theme.muted
            font.pixelSize: 13
            wrapMode: Text.WordWrap
        }
    }

    // One Card per discovered server. The Repeater lays them out inside the
    // Page's default Column, so each Card is a sibling stacking row.
    Repeater {
        model: App.discoveredServers
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

                // `page.pairTick` is read (comma-expression) only to enlist this
                // binding in the stateChanged dependency graph — isPairingInFlight
                // has no NOTIFY of its own, so without it the spinner never clears.
                Kit.KitButton {
                    id: pairButton
                    text: (page.pairTick, App.isPairingInFlight(foundCard.modelData.id))
                          ? qsTr("Pairing…") : qsTr("Pair")
                    enabled: !(page.pairTick, App.isPairingInFlight(foundCard.modelData.id))
                    onClicked: pairDialog.openFor(foundCard.modelData.id,
                                                  foundCard.modelData.name.length > 0
                                                      ? foundCard.modelData.name
                                                      : foundCard.modelData.ip)

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: (page.pairTick, App.isPairingInFlight(foundCard.modelData.id))
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
            required property string linkState
            required property string chip
            required property string dotColor
            required property string glyph
            required property bool liveLink
            required property string latencyText
            required property int latencySamples

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

                ColumnLayout {
                    spacing: 2
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
                    // One-way latency caption (median heartbeat-RTT/2 over the
                    // sliding window); the sample count travels with the figure
                    // so a barely-seeded window reads as tentative. Shown only
                    // while the link is online and the window has samples.
                    Label {
                        visible: rememberedCard.linkState === "connected"
                                 && rememberedCard.latencySamples > 0
                        text: qsTr("%1 · last %2 pings")
                                  .arg(rememberedCard.latencyText)
                                  .arg(rememberedCard.latencySamples)
                        color: Theme.muted
                        font.pixelSize: 11
                        Layout.alignment: Qt.AlignRight
                    }
                }

                // Live ⇔ not-live are mutually exclusive: a live row offers
                // Disconnect, an offline row offers Connect (reconnect). Both
                // gate purely on reactive model roles so they track the row's
                // chip/dot without any non-reactive invokable.
                Kit.OutlineButton {
                    text: qsTr("Disconnect")
                    visible: rememberedCard.liveLink
                    onClicked: App.disconnectConnection(rememberedCard.connectionId)
                }

                Kit.KitButton {
                    text: rememberedCard.linkState === "connecting" ? qsTr("Connecting…")
                                                                    : qsTr("Connect")
                    visible: !rememberedCard.liveLink
                    // Disabled mid-connect: reconnectConnection is gated on the
                    // row not yet being live, and a second kick while connecting
                    // is meaningless.
                    enabled: !rememberedCard.liveLink
                             && rememberedCard.linkState !== "connecting"
                    onClicked: App.reconnectConnection(rememberedCard.connectionId)

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: rememberedCard.linkState === "connecting"
                        visible: running
                        implicitWidth: 20
                        implicitHeight: 20
                    }
                }

                Kit.OutlineButton {
                    text: qsTr("Forget")
                    onClicked: App.forgetConnection(rememberedCard.connectionId)
                }
            }
        }
    }

    // ---- PAIRING dialog (both directions, one shot) ------------------------

    // A discovered satellite is unpaired, so Pair is its only action. Opening the
    // sheet immediately SENDS the reverse pair request — it shows the operator a
    // PIN to type on the satellite and starts polling for approval — AND offers a
    // field to type the satellite's own PIN (forward). Whichever path completes
    // first pairs; closing aborts the still-pending reverse request.
    Kit.ContentDialog {
        id: pairDialog
        heading: qsTr("Pair with %1").arg(pairDialog.serverName)
        rejectText: qsTr("Cancel")

        property string serverId: ""
        property string serverName: ""
        property bool submitted: false        // forward submit guard
        property string errorText: ""         // forward inline error

        // The footer drives the FORWARD path (typing the satellite's PIN); the
        // reverse path completes on its own when the operator approves. pairTick
        // enlists the non-reactive isPairingInFlight() in the stateChanged graph.
        acceptText: (page.pairTick, App.isPairingInFlight(pairDialog.serverId)) ? qsTr("Pairing…")
                                                                                : qsTr("Pair")
        acceptEnabled: pinField.text.length === 4
                       && !(page.pairTick, App.isPairingInFlight(pairDialog.serverId))

        function openFor(id, name) {
            pairDialog.serverId = id;
            pairDialog.serverName = name;
            pairDialog.errorText = "";
            pairDialog.submitted = false;
            pinField.clear();
            App.clearPairingTarget();          // drop any parked trigger (avoid re-entry)
            // Send the initial (reverse) pair now: generates + shows our PIN and
            // starts polling for the operator to approve on the satellite.
            App.requestReversePairing(id);
            pairDialog.open();
        }

        contentColumn.children: [
            // ── REVERSE: the PIN to type on the satellite (sent on open) ──
            Label {
                text: qsTr("Show this PIN on %1 to approve").arg(pairDialog.serverName)
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Label {
                text: App.reversePairingPhase === "timedout" ? qsTr("expired")
                                                             : App.reversePairingPin
                color: App.reversePairingPhase === "timedout" ? Theme.muted : Theme.onSurface
                font.pixelSize: 36
                font.bold: true
                font.letterSpacing: 6
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            },
            // Reverse status line: waiting / declined / expired, with regenerate.
            RowLayout {
                spacing: 8
                Layout.fillWidth: true
                BusyIndicator {
                    running: App.reversePairingPhase === "awaiting"
                    visible: running
                    implicitWidth: 16
                    implicitHeight: 16
                }
                Label {
                    Layout.fillWidth: true
                    color: App.reversePairingPhase === "declined" ? Theme.error : Theme.muted
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                    text: App.reversePairingPhase === "awaiting"
                              ? qsTr("Waiting for %1 to accept…").arg(pairDialog.serverName)
                          : App.reversePairingPhase === "declined" ? qsTr("Declined on the satellite.")
                          : App.reversePairingPhase === "timedout" ? qsTr("That code expired.")
                          : ""
                }
                Kit.OutlineButton {
                    text: qsTr("New code")
                    visible: App.reversePairingPhase === "timedout"
                             || App.reversePairingPhase === "declined"
                    onClicked: App.requestReversePairing(pairDialog.serverId)
                }
            },

            // Divider between the two directions.
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.outline },

            // ── FORWARD: type the satellite's own PIN ──
            Label {
                text: qsTr("…or enter the PIN shown on %1").arg(pairDialog.serverName)
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: pinField
                placeholderText: qsTr("4-digit PIN")
                maximumLength: 4
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0 }
                enabled: !(page.pairTick, App.isPairingInFlight(pairDialog.serverId))
                Layout.fillWidth: true
            },
            Label {
                visible: pairDialog.errorText.length > 0
                text: pairDialog.errorText
                color: Theme.error
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        ]

        // Footer = the FORWARD submit. Reverse completes on its own (approval).
        onAccepted: {
            pairDialog.submitted = true;
            // Id-based: de-raced against a concurrent scan reordering the list.
            App.pairByServerId(pairDialog.serverId, pinField.text);
        }
        onRejected: App.cancelReversePairing()

        Connections {
            target: App
            enabled: pairDialog.visible

            // Forward: keep the sheet OPEN on error and surface why.
            function onErrorMessage(message) {
                pairDialog.submitted = false;
                pairDialog.errorText = message;
            }
            // Forward success: a submission is no longer in flight and no error
            // vetoed it → close.
            function onStateChanged() {
                if (pairDialog.submitted && !App.isPairingInFlight(pairDialog.serverId)) {
                    pairDialog.submitted = false;
                    pairDialog.close();
                }
            }
            // Reverse approved: the session is opening (the row goes live) → close.
            function onReversePairingChanged() {
                if (App.reversePairingPhase === "approved")
                    pairDialog.close();
            }
        }

        onClosed: {
            // Abort a still-pending reverse request so no orphan poll outlives the sheet.
            if (App.reversePairingPhase !== "approved")
                App.cancelReversePairing();
            pinField.clear();
            pairDialog.errorText = "";
            pairDialog.submitted = false;
        }
    }

}
