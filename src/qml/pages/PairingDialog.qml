// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The shared pairing sheet (design FPairDlg + FReverseDlg fused; android
// PairPinDialog parity): BOTH pairing paths run in one dialog and no tap gates
// either. Path A — the forward 6-digit satellite-PIN field — is always
// typeable; Path B — our reverse 4-digit clientPin — is POSTed automatically
// by openFor() (the operator is notified the moment the sheet opens), shown
// under an "or" divider, and polled (~2 min budget) until the operator
// approves on the satellite. Whichever path completes first pairs the box;
// the sheet closes itself on success (pairingSucceeded / phase "approved").
//
// Used by ConnectionsPage (the Pair… rows + the parked-target auto-open) and
// the setup-guide wizard's host rows. All behavior forwards to App
// (QML_CONTRACT.md "The pairing sheet: both paths at once").

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.ContentDialog {
    id: pairDialog

    property string serverId: ""
    property string serverName: ""
    property bool submitting: false   // forward submit in flight

    // Path B is on screen: we actually kicked a reverse request for this
    // sheet (openFor cancels any stale one first, so idle == not ours).
    readonly property bool reverseLive:
        serverId.length > 0 && App.reversePairingPhase !== "idle"

    eyebrow: qsTr("Pairing")
    heading: qsTr("Pair with %1").arg(pairDialog.serverName)
    preferredWidth: 400
    rejectText: qsTr("Cancel")
    acceptText: pairDialog.submitting ? qsTr("Pairing…") : qsTr("Pair")
    acceptEnabled: pinField.text.length === 6 && !pairDialog.submitting

    function openFor(id, name) {
        serverId = id;
        serverName = name;
        submitting = false;
        pinField.clear();
        // Send our PIN to the satellite immediately — the operator sees the
        // request the moment the sheet opens, while the PIN field stays a
        // live fallback. Cancel first so a stale phase from an earlier
        // sheet can never leak in when this id doesn't resolve.
        App.cancelReversePairing();
        if (id.length > 0)
            App.requestReversePairing(id);
        open();
    }

    body: [
        // ── Path A: type the satellite's PIN ──
        Label {
            text: qsTr("Enter the 6-digit PIN shown on the Satellite's screen.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        Kit.KitTextField {
            id: pinField
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

        // ── divider ──
        RowLayout {
            visible: pairDialog.reverseLive
            spacing: Tokens.s4
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s2

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.outline }
            Label {
                text: qsTr("or")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.outline }
        },

        // ── Path B: approve on the satellite (sent automatically on open) ──
        Label {
            visible: pairDialog.reverseLive
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
            visible: pairDialog.reverseLive
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
            visible: pairDialog.reverseLive
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

    // Footer accept = the Path-A submit (typeable throughout Path B's
    // wait). Errors keep the sheet open (the global toast carries the
    // message); success closes below.
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
    }
}
