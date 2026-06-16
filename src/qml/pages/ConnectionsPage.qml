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

                Kit.OutlineButton {
                    text: qsTr("Connect")
                    // Id-based (de-raced): a scan reordering the list between bind
                    // and click can't connect the wrong box.
                    onClicked: App.connectByServerId(foundCard.modelData.id)
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

                // Reverse (host-initiated) pairing: the dish shows a PIN the
                // operator approves on the satellite. requestReversePairing then
                // open — the sheet drives off App.reversePairingPhase.
                Kit.OutlineButton {
                    text: qsTr("Request pairing")
                    onClicked: reverseDialog.openFor(foundCard.modelData.id,
                                                     foundCard.modelData.name.length > 0
                                                         ? foundCard.modelData.name
                                                         : foundCard.modelData.ip)
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

    // ---- PAIRING dialog ----------------------------------------------------

    Kit.ContentDialog {
        id: pairDialog
        heading: qsTr("Pair with %1").arg(pairDialog.serverName)
        acceptText: App.isPairingInFlight(pairDialog.serverId) ? qsTr("Pairing…") : qsTr("Pair")
        rejectText: qsTr("Cancel")
        // Enable accept only on a full 4-digit PIN and while no request is in
        // flight for this server.
        acceptEnabled: pinField.text.length === 4 && !App.isPairingInFlight(pairDialog.serverId)

        // The discovered server this dialog targets — set by openFor() before
        // open(). Id-based (de-raced): the contract calls address the row by its
        // stable id, not a list index that a concurrent scan could shift.
        property string serverId: ""
        property string serverName: ""

        function openFor(id, name) {
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
                text: qsTr("Enter the 4-digit PIN displayed on %1").arg(pairDialog.serverName)
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: pinField
                placeholderText: qsTr("4-digit PIN")
                maximumLength: 4
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
            // Id-based: de-raced against a concurrent scan reordering the list.
            App.pairByServerId(pairDialog.serverId, pinField.text);
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

    // ---- REVERSE-PAIRING dialog --------------------------------------------

    // Host-initiated pairing: the dish generates + shows a PIN; the operator
    // types it on the satellite. Everything visible is driven off the reactive
    // App.reversePairing* properties (NOTIFY reversePairingChanged), so a phase
    // move ("awaiting"→"approved"/"declined"/"timedout") repaints the body and
    // re-gates the footer without any manual polling here.
    Kit.ContentDialog {
        id: reverseDialog
        heading: qsTr("Pair with %1").arg(App.reversePairingServerName)
        // Only Cancel acts while awaiting; the approved arm auto-closes, the
        // terminal-error arms offer Retry. Accept is the Retry affordance and is
        // only meaningful once the request has timed out.
        acceptText: qsTr("Retry")
        rejectText: qsTr("Cancel")
        acceptEnabled: App.reversePairingPhase === "timedout"

        // Set by openFor() before open(); kept for the Retry path so a retry
        // re-targets the same server without re-reading the (possibly reordered)
        // discovered list.
        property string serverId: ""

        function openFor(id, name) {
            reverseDialog.serverId = id;
            // Kicks the request; the dish PIN + phase land on reversePairingChanged.
            App.requestReversePairing(id);
            reverseDialog.open();
        }

        contentColumn.children: [
            // The locally-generated PIN, shown big while awaiting.
            Label {
                text: App.reversePairingPin
                visible: App.reversePairingPhase === "awaiting"
                color: Theme.onSurface
                font.pixelSize: 40
                font.bold: true
                font.letterSpacing: 8
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            },
            Label {
                text: qsTr("Enter this PIN on %1 to approve.").arg(App.reversePairingServerName)
                visible: App.reversePairingPhase === "awaiting"
                color: Theme.muted
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                Layout.fillWidth: true
            },
            RowLayout {
                visible: App.reversePairingPhase === "awaiting"
                spacing: 8
                Layout.fillWidth: true
                BusyIndicator {
                    running: App.reversePairingPhase === "awaiting"
                    implicitWidth: 18
                    implicitHeight: 18
                }
                Label {
                    text: qsTr("Waiting for %1 to accept…").arg(App.reversePairingServerName)
                    color: Theme.muted
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            },
            Label {
                text: qsTr("Paired — the connection is opening.")
                visible: App.reversePairingPhase === "approved"
                color: Theme.success
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Label {
                text: qsTr("Request declined.")
                visible: App.reversePairingPhase === "declined"
                color: Theme.error
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Label {
                text: qsTr("Timed out — try again.")
                visible: App.reversePairingPhase === "timedout"
                color: Theme.error
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        ]

        // Accept here is Retry (enabled only on timedout): re-kick the same
        // server and fall back into the awaiting arm.
        onAccepted: {
            if (reverseDialog.serverId.length > 0)
                App.requestReversePairing(reverseDialog.serverId);
        }
        // Cancel aborts the in-flight request and closes (rejected auto-closes).
        onRejected: App.cancelReversePairing()

        Connections {
            target: App
            enabled: reverseDialog.visible

            // Auto-close on the approved edge (session is opening; the row goes
            // live). The declined/timedout arms stay open so the operator can
            // read the reason and Retry/Cancel.
            function onReversePairingChanged() {
                if (App.reversePairingPhase === "approved")
                    reverseDialog.close();
            }
        }
    }
}
