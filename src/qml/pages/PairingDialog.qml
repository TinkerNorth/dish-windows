// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The shared pairing sheet — ONE sheet, two callers (the wizard's Destination
// step and Connections' found rows). BOTH pairing paths run in it at once and
// no tap gates either. Path A — the forward 6-digit satellite PIN — is always
// typeable; Path B — our reverse 4-digit clientPin — is POSTed automatically by
// openFor() (the operator is notified the moment the sheet opens), shown under
// an "or" divider, and polled (~2 min budget) until the operator approves on
// the satellite. Whichever path completes first pairs the box; the sheet closes
// itself on success (pairingSucceeded / phase "approved").
//
// The PIN is SIX DISCRETE CELLS over one hidden capture field, not a
// letter-spaced text box: Qt does not composite trailing letter-spacing the way
// CSS does, and a letter-spaced run of digits is announced badly by a screen
// reader. The field is numeric-only and paste-tolerant (spaces and dashes are
// stripped), and Pair stays disabled until all six digits are in.
//
// A REJECTION KEEPS THE SHEET OPEN and marks the field inline. It does not go
// to the toast: the toast is for transient failures the user cannot act on, and
// this one has the user's own next action attached to it.

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

    // ── Forward-PIN rejection (inline, never a toast) ───────────────────────
    property bool pinRejected: false
    property string pinReason: ""
    // The third rejection earns the extra line: by then the likeliest cause is
    // that the satellite re-drew its screen and minted a new code.
    property int rejections: 0

    readonly property int pinLength: 6

    readonly property string pinErrorText: {
        if (!pairDialog.pinRejected)
            return "";
        let message;
        if (pairDialog.pinReason === "versionMismatch") {
            message = qsTr("%1 is running a different Satellite version. Update both, then pair again.")
                        .arg(pairDialog.serverName);
        } else if (pairDialog.pinReason === "unreachable") {
            message = qsTr("Couldn’t reach %1. Make sure it’s on and on your network.")
                        .arg(pairDialog.serverName);
        } else if (pairDialog.pinReason === "pending") {
            message = qsTr("%1 hasn’t approved the pairing yet.").arg(pairDialog.serverName);
        } else {
            message = qsTr("That PIN wasn’t accepted. Check the code on %1 and try again.")
                        .arg(pairDialog.serverName);
        }
        if (pairDialog.rejections >= 3) {
            message += " " + qsTr("The code changes if the Satellite screen was refreshed.");
        }
        return message;
    }

    eyebrow: qsTr("Pairing")
    heading: qsTr("Pair with %1").arg(pairDialog.serverName)
    preferredWidth: 430
    rejectText: qsTr("Cancel")
    acceptText: pairDialog.submitting ? qsTr("Pairing…") : qsTr("Pair")
    acceptEnabled: pinInput.text.length === pairDialog.pinLength && !pairDialog.submitting

    // The sheet owns the keyboard while it is up, and Popup hands focus back to
    // the row that opened it on close.
    focus: true

    function openFor(id, name) {
        serverId = id;
        serverName = name;
        submitting = false;
        pinRejected = false;
        pinReason = "";
        rejections = 0;
        pinInput.clear();
        // Send our PIN to the satellite immediately — the operator sees the
        // request the moment the sheet opens, while the PIN field stays a
        // live fallback. Cancel first so a stale phase from an earlier
        // sheet can never leak in when this id doesn't resolve.
        App.cancelReversePairing();
        if (id.length > 0)
            App.requestReversePairing(id);
        open();
    }

    onOpened: pinInput.forceActiveFocus()

    body: [
        // ── Path A: type the satellite's PIN ──
        Label {
            text: qsTr("Enter the 6-digit PIN shown on the Satellite’s screen.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },

        // Six cells drawn over one hidden capture field: the field owns paste,
        // the input hints and the caret position; the cells only render it.
        Item {
            id: pinBlock
            implicitHeight: cellRow.implicitHeight
            Layout.fillWidth: true

            Row {
                id: cellRow
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Tokens.s4

                Repeater {
                    model: pairDialog.pinLength

                    delegate: Rectangle {
                        id: cell
                        required property int index

                        // The cell the next digit lands in carries the caret.
                        readonly property bool armed: pinInput.activeFocus
                                                      && cell.index === Math.min(
                                                          pinInput.text.length,
                                                          pairDialog.pinLength - 1)

                        implicitWidth: 40
                        implicitHeight: 48
                        radius: Tokens.radiusButton
                        color: Theme.surfaceDim
                        border.width: 1
                        border.color: pairDialog.pinRejected ? Theme.error
                                    : cell.armed ? Theme.primary
                                    : Theme.outline

                        Label {
                            anchors.centerIn: parent
                            text: cell.index < pinInput.text.length
                                  ? pinInput.text.charAt(cell.index) : ""
                            color: Theme.onSurface
                            font.family: Tokens.monoFamily
                            font.pixelSize: Tokens.textHero
                        }
                    }
                }
            }

            // Invisible, but live: it takes the clicks, the focus, the paste
            // and the input-method hints for the whole block.
            TextInput {
                id: pinInput
                anchors.fill: cellRow
                opacity: 0
                enabled: !pairDialog.submitting
                inputMethodHints: Qt.ImhDigitsOnly
                // Deliberately longer than the PIN: maximumLength truncates a
                // pasted "408 192" BEFORE the separators are stripped.
                maximumLength: 24
                activeFocusOnTab: true

                Accessible.role: Accessible.EditableText
                Accessible.name: qsTr("Pairing PIN, 6 digits")
                Accessible.description: pairDialog.pinErrorText

                onTextChanged: {
                    const cleaned = pinInput.text.replace(/[^0-9]/g, "")
                                                 .substring(0, pairDialog.pinLength);
                    if (cleaned !== pinInput.text)
                        pinInput.text = cleaned;
                    // Typing is the user answering the error; clear it.
                    if (pairDialog.pinRejected)
                        pairDialog.pinRejected = false;
                }

                onAccepted: {
                    if (pairDialog.acceptEnabled)
                        pairDialog.accepted();
                }
            }
        },

        Label {
            visible: pairDialog.pinErrorText.length > 0
            text: pairDialog.pinErrorText
            color: Theme.error
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
            Layout.fillWidth: true

            Accessible.role: Accessible.AlertMessage
            Accessible.name: text
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
                    id: reverseCell
                    required property int index

                    implicitWidth: 40
                    implicitHeight: 48
                    radius: Tokens.radiusButton
                    color: Theme.surfaceDim
                    border.width: 1
                    border.color: Theme.outline

                    Label {
                        anchors.centerIn: parent
                        text: reverseCell.index < App.reversePairingPin.length
                              ? App.reversePairingPin.charAt(reverseCell.index) : ""
                        color: Theme.primary
                        font.family: Tokens.monoFamily
                        font.pixelSize: Tokens.textHero
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
            Kit.DishButton {
                text: qsTr("New code")
                variant: Kit.DishButton.Outline
                size: Kit.DishButton.Small
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
    // wait). Errors keep the sheet open (the inline message carries the
    // reason); success closes below.
    onAccepted: {
        submitting = true;
        pinRejected = false;
        App.pairByServerId(serverId, pinInput.text);
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
        // Keyed by the STABLE server id: a rejection for some other box must
        // never mark this sheet's field.
        function onPairingFailed(failedServerId, reasonToken) {
            if (failedServerId !== pairDialog.serverId)
                return;
            pairDialog.submitting = false;
            pairDialog.rejections += 1;
            pairDialog.pinReason = reasonToken;
            pairDialog.pinRejected = true;
            pinInput.forceActiveFocus();
            pinInput.selectAll();
        }
    }

    onClosed: {
        // No orphan poll outlives the sheet.
        if (App.reversePairingPhase !== "approved")
            App.cancelReversePairing();
        pinInput.clear();
        submitting = false;
        pinRejected = false;
        rejections = 0;
    }
}
