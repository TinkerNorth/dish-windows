// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Connections destination (design flow 02): FOUND (scan + discovered rows)
// and REMEMBERED (link-state rows with latency + per-state actions), plus the
// pairing dialog — forward 6-digit PIN entry with a link that flips to the
// reverse 4-digit view (the operator approves on the satellite). All behavior
// forwards to App (QML_CONTRACT.md A2); this file holds zero business logic.
//
// The old pairTick polling hack is gone: submission progress is a dialog-local
// flag resolved by pairingSucceeded / errorMessage, and everything else binds
// reactive properties (scanning, discoveredServers, reversePairing*).

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page

    // ── Shell header (rendered by AppShell, not here) ────────────────────────
    readonly property string headerTitle: qsTr("Connections")
    readonly property string headerSub: App.connectionCount === 0
        ? qsTr("%1 found · nothing remembered yet").arg(App.foundCount)
        : qsTr("%1 streaming · %2 remembered").arg(App.onlineCount).arg(App.connectionCount)
    readonly property string headerDot: App.connectionCount === 0 ? "muted"
                                        : App.onlineCount > 0 ? "success" : "warning"

    // Scan on open (android #125 parity): entering the destination surfaces
    // reachable satellites without an extra Scan tap; startDiscovery() is
    // guarded manager-side so an in-flight scan is never double-triggered.
    Component.onCompleted: if (!App.scanning) App.startDiscovery()

    // A satellite can park a pairing request C++-side (a stale-key box we tried
    // to talk to). Open the sheet for it exactly once per parked target.
    Connections {
        target: App
        function onStateChanged() {
            if (App.pairingActive && !pairDialog.visible) {
                const name = App.pairingServerName;
                App.clearPairingTarget();
                pairDialog.openFor("", name);
            }
        }
    }

    // Localized chip text for a ConnectionListModel `chip` token. Pure
    // presentation — the C++ vends the token, not the copy.
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

    // Chip tone (design chipToneColor): online green, transient amber, ready
    // accent, the rest muted.
    function chipColor(token) {
        switch (token) {
        case "online":       return Theme.success;
        case "connecting":   return Theme.warning;
        case "unstable":     return Theme.warning;
        case "needsPairing": return Theme.warning;
        case "ready":        return Theme.primary;
        default:             return Theme.muted;
        }
    }

    // Kit.Page's default slot is a plain Column (Layout.* attached props are
    // inert there), so the whole body rides ONE ColumnLayout pinned to the
    // page width — everything inside uses Layout.* normally.
    ColumnLayout {
        width: parent.width
        spacing: Tokens.s5

    // ── FOUND ────────────────────────────────────────────────────────────────

    RowLayout {
        Layout.fillWidth: true
        spacing: Tokens.s4

        Kit.SectionHeader { glyph: "satellite"; label: qsTr("Found") }
        Item { Layout.fillWidth: true }
        Kit.OutlineButton {
            text: App.scanning ? qsTr("Scanning…") : qsTr("Scan")
            enabled: !App.scanning
            onClicked: App.startDiscovery()
        }
    }

    Kit.DishProgressBar {
        Layout.fillWidth: true
        visible: App.scanning
        indeterminate: true
    }

    // The FOUND list panel: one surface card, hairline-divided rows.
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: foundColumn.implicitHeight
        radius: Tokens.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
        clip: true

        ColumnLayout {
            id: foundColumn
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            Label {
                visible: App.discoveredServers.length === 0
                text: App.scanning ? qsTr("Searching your LAN…")
                                   : qsTr("No satellites found yet — hit Scan to look again.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                Layout.fillWidth: true
                Layout.margins: Tokens.s7
            }

            Repeater {
                model: App.discoveredServers
                delegate: ColumnLayout {
                    id: foundRow
                    required property int index
                    required property var modelData
                    readonly property string displayName:
                        modelData.name.length > 0 ? modelData.name : modelData.ip

                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        visible: foundRow.index > 0
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.outline
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: Tokens.s5
                        Layout.bottomMargin: Tokens.s5
                        Layout.leftMargin: Tokens.s7
                        Layout.rightMargin: Tokens.s7
                        spacing: Tokens.s5

                        Kit.BrandGlyph {
                            glyph: "satellite"
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: foundRow.displayName
                                color: Theme.onSurface
                                font.pixelSize: Tokens.textBase
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: foundRow.modelData.ip + " · " + foundRow.modelData.source
                                color: Theme.muted
                                font.family: Tokens.monoFamily
                                font.pixelSize: Tokens.textMeta
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Kit.KitButton {
                            text: qsTr("Pair…")
                            onClicked: pairDialog.openFor(foundRow.modelData.id,
                                                          foundRow.displayName)
                        }
                    }
                }
            }
        }
    }

    // ── REMEMBERED ───────────────────────────────────────────────────────────

    Kit.SectionHeader {
        glyph: "satellite"
        label: qsTr("Remembered")
        Layout.topMargin: Tokens.s2
    }

    Rectangle {
        Layout.fillWidth: true
        implicitHeight: rememberedColumn.implicitHeight
        radius: Tokens.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
        clip: true

        ColumnLayout {
            id: rememberedColumn
            anchors.left: parent.left
            anchors.right: parent.right
            spacing: 0

            Label {
                visible: rememberedRepeater.count === 0
                text: qsTr("No remembered satellites yet — pair one and it is saved here.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                Layout.fillWidth: true
                Layout.margins: Tokens.s7
            }

            Repeater {
                id: rememberedRepeater
                model: App.connectionModel
                delegate: ColumnLayout {
                    id: row

                    // ConnectionListModel roles (QML_CONTRACT.md §3).
                    required property int index
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

                    readonly property bool needsPairing: chip === "needsPairing"
                    readonly property bool connecting: linkState === "connecting"
                    readonly property bool showLatency:
                        latencySamples > 0 && (linkState === "connected"
                                               || chip === "unstable")

                    Layout.fillWidth: true
                    spacing: 0

                    Rectangle {
                        visible: row.index > 0
                        Layout.fillWidth: true
                        implicitHeight: 1
                        color: Theme.outline
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 9
                        Layout.bottomMargin: 9
                        Layout.leftMargin: Tokens.s7
                        Layout.rightMargin: Tokens.s7
                        spacing: Tokens.s5

                        Kit.BrandGlyph {
                            id: rowGlyph
                            glyph: rowGlyph.glyphForToken(row.glyph)
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                        }
                        Kit.StatusDot { token: row.dotColor }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                text: row.label
                                color: Theme.onSurface
                                font.pixelSize: Tokens.textBase
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            RowLayout {
                                spacing: Tokens.s3
                                Layout.fillWidth: true
                                Label {
                                    text: qsTr("%1 • UDP %2").arg(row.ip).arg(row.udpPort)
                                    color: Theme.muted
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: Tokens.textMeta
                                    elide: Text.ElideRight
                                }
                                Label {
                                    visible: row.showLatency
                                    text: qsTr("%1 · last %2 pings")
                                              .arg(row.latencyText).arg(row.latencySamples)
                                    color: Theme.success
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: Tokens.textMeta
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Item { visible: !row.showLatency; Layout.fillWidth: true }
                            }
                        }

                        Label {
                            text: page.chipText(row.chip)
                            color: page.chipColor(row.chip)
                            font.pixelSize: Tokens.textMeta
                            rightPadding: Tokens.s2
                        }

                        // Per-state action (design): live → Disconnect; needs
                        // pairing → Pair…; else Reconnect (disabled mid-connect).
                        Kit.OutlineButton {
                            text: qsTr("Disconnect")
                            visible: row.liveLink
                            onClicked: App.disconnectConnection(row.connectionId)
                        }
                        Kit.KitButton {
                            text: qsTr("Pair…")
                            visible: !row.liveLink && row.needsPairing
                            onClicked: pairDialog.openFor(row.connectionId, row.label)
                        }
                        Kit.OutlineButton {
                            text: row.connecting ? qsTr("Connecting…") : qsTr("Reconnect")
                            visible: !row.liveLink && !row.needsPairing
                            enabled: !row.connecting
                            onClicked: App.reconnectConnection(row.connectionId)
                        }
                        Kit.OutlineButton {
                            text: qsTr("Forget")
                            onClicked: App.forgetConnection(row.connectionId)
                        }
                    }
                }
            }
        }
    }

    } // body ColumnLayout

    // ── PAIRING dialog: forward PIN entry, flip-to-reverse ───────────────────
    // Forward (design FPairDlg): type the satellite's 6-digit operator PIN.
    // The link flips to the reverse view (design FReverseDlg): we POST a
    // 4-digit clientPin, show it, and poll until the operator approves on the
    // satellite (~2 min budget). Whichever path completes pairs the box.
    Kit.ContentDialog {
        id: pairDialog

        property string serverId: ""
        property string serverName: ""
        property bool reverseMode: false
        property bool submitting: false   // forward submit in flight

        eyebrow: qsTr("Pairing")
        heading: pairDialog.reverseMode ? qsTr("Pair from this PC")
                                        : qsTr("Pair with %1").arg(pairDialog.serverName)
        preferredWidth: 400
        rejectText: qsTr("Cancel")
        acceptText: pairDialog.reverseMode ? ""
                   : pairDialog.submitting ? qsTr("Pairing…") : qsTr("Pair")
        acceptEnabled: pinField.text.length === 6 && !pairDialog.submitting

        function openFor(id, name) {
            serverId = id;
            serverName = name;
            reverseMode = false;
            submitting = false;
            pinField.clear();
            open();
        }

        body: [
            // ── FORWARD ──
            Label {
                visible: !pairDialog.reverseMode
                text: qsTr("Enter the 6-digit PIN shown on the Satellite's screen.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: pinField
                visible: !pairDialog.reverseMode
                maximumLength: 6
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 0 }
                enabled: !pairDialog.submitting
                font.family: Tokens.monoFamily
                font.pixelSize: 20
                font.letterSpacing: 10
                horizontalAlignment: TextInput.AlignHCenter
                implicitHeight: 44
                Layout.fillWidth: true
            },
            Label {
                visible: !pairDialog.reverseMode && pairDialog.serverId.length > 0
                text: qsTr("Show a PIN on this PC instead…")
                color: Theme.primary
                font.pixelSize: Tokens.textSummary
                font.underline: reverseLinkHover.hovered
                Layout.fillWidth: true

                HoverHandler { id: reverseLinkHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: {
                        pairDialog.reverseMode = true;
                        App.requestReversePairing(pairDialog.serverId);
                    }
                }
            },

            // ── REVERSE ──
            Label {
                visible: pairDialog.reverseMode
                textFormat: Text.StyledText
                text: qsTr("Type this PIN on <b>%1</b> to approve the pairing.")
                          .arg(pairDialog.serverName)
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            RowLayout {
                visible: pairDialog.reverseMode
                spacing: Tokens.s4
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Tokens.s3
                Layout.bottomMargin: Tokens.s3

                Repeater {
                    model: 4
                    delegate: Rectangle {
                        required property int index
                        implicitWidth: 44
                        implicitHeight: 52
                        radius: Tokens.radiusButton
                        color: Theme.surfaceDim
                        border.width: 1
                        border.color: Theme.outline

                        Label {
                            anchors.centerIn: parent
                            text: index < App.reversePairingPin.length
                                  ? App.reversePairingPin.charAt(index) : ""
                            color: Theme.primary
                            font.family: Tokens.monoFamily
                            font.pixelSize: 22
                        }
                    }
                }
            },
            RowLayout {
                visible: pairDialog.reverseMode
                spacing: Tokens.s5
                Layout.fillWidth: true

                Kit.DishProgressBar {
                    visible: App.reversePairingPhase === "awaiting"
                    indeterminate: true
                    Layout.preferredWidth: 60
                }
                Label {
                    text: App.reversePairingPhase === "awaiting"
                              ? qsTr("Waiting for approval on the satellite…")
                          : App.reversePairingPhase === "approved"
                              ? qsTr("Approved — connecting…")
                          : App.reversePairingPhase === "declined"
                              ? qsTr("The operator declined the pairing.")
                          : App.reversePairingPhase === "timedout"
                              ? qsTr("No approval — the code expired.")
                          : ""
                    color: App.reversePairingPhase === "declined" ? Theme.error
                         : App.reversePairingPhase === "approved" ? Theme.success
                         : Theme.muted
                    font.pixelSize: Tokens.textSummary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Kit.OutlineButton {
                    text: qsTr("New code")
                    visible: App.reversePairingPhase === "declined"
                             || App.reversePairingPhase === "timedout"
                    onClicked: App.requestReversePairing(pairDialog.serverId)
                }
                Label {
                    visible: App.reversePairingPhase === "awaiting"
                    text: qsTr("~2 min")
                    color: Theme.muted
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                }
            }
        ]

        // Footer accept = the FORWARD submit (hidden in reverse mode). Errors
        // keep the sheet open (the global toast carries the message); success
        // closes below.
        onAccepted: {
            submitting = true;
            App.pairByServerId(serverId, pinField.text);
        }
        onRejected: App.cancelReversePairing()

        Connections {
            target: App
            enabled: pairDialog.visible

            function onErrorMessage(message) { pairDialog.submitting = false; }
            function onPairingSucceeded() { pairDialog.close(); }
            function onReversePairingChanged() {
                if (App.reversePairingPhase === "approved")
                    pairDialog.close();
            }
        }

        onClosed: {
            // No orphan poll outlives the sheet.
            if (App.reversePairingPhase !== "approved")
                App.cancelReversePairing();
            pinField.clear();
            submitting = false;
            reverseMode = false;
        }
    }
}
