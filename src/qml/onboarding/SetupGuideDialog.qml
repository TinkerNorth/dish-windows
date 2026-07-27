// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The 3-step setup WIZARD as an in-window dialog over the shell (design frame
// f-a2, made functional in the android-parity pass — the port of android's
// guided setup, its connection step in particular): Connect — a LIVE scan +
// pick + pair flow (rescan, host rows with Pair…, the get-Satellite empty
// state, auto-advance when the user's own pairing lands) · Controller — the
// live detected-pad list (no on-screen pad on Windows) · Summary. Opened from
// the Settings "Setup guide" row and the welcome hand-off; closing is always
// allowed (re-run any time).

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import "../pages" as Pages
import Dish.Chrome

Kit.ContentDialog {
    id: guide

    property int step: 0
    readonly property int stepCount: 3

    // The host the user tapped Pair… on. Android's SetupConnectionViewModel
    // rule, ported: only the USER'S pairing advances the wizard — a background
    // auto-reconnect going live on its own must not yank the user forward.
    // Approximate on Windows in that the success edge (pairingSucceeded) is
    // not per-host; the gate is that a pair the user started HERE is pending.
    property string pendingHostId: ""

    // Same download page the welcome pager links (android: the GitHub button
    // on the empty satellite list).
    readonly property string satelliteUrl: "https://dish.tinkernorth.com/downloads/satellite"

    eyebrow: qsTr("Setup guide · step %1 of %2").arg(step + 1).arg(stepCount)
    heading: step === 0 ? qsTr("Find and pair your Satellite")
           : step === 1 ? qsTr("Plug in a controller")
           : qsTr("You're set")
    acceptText: step < stepCount - 1 ? qsTr("Next") : qsTr("Finish")
    rejectText: qsTr("Close")
    preferredWidth: 470

    onOpened: {
        step = 0;
        pendingHostId = "";
        // Scan immediately (android #125 parity): the wizard's first step IS
        // the picker — no extra tap to start looking. Guarded manager-side
        // against double-trigger.
        if (!App.scanning)
            App.startDiscovery();
    }
    onAccepted: {
        if (step < stepCount - 1)
            step += 1;
        else
            close();
    }

    // Auto-advance: the user's pairing landed (their satellite came online) →
    // move on to the controller step. Declared BEFORE wizardPair so this
    // handler sees the shared signal first (connection order) while the pair
    // sheet is still deciding to close itself.
    Connections {
        target: App
        enabled: guide.visible

        function onPairingSucceeded() {
            if (guide.step === 0 && guide.pendingHostId.length > 0) {
                guide.pendingHostId = "";
                guide.step = 1;
            }
        }
    }

    // The wizard's own pairing sheet — the shared both-paths-at-once dialog
    // (type the satellite's PIN, or the operator approves our auto-sent one).
    // A Popup reparents to Overlay.overlay on open, so it stacks above this
    // dialog as the topmost modal.
    Pages.PairingDialog { id: wizardPair }

    // The design's option card: title + body on a surface card; `stroked`
    // accents the card that names the action the user takes next.
    component OptionCard: Rectangle {
        id: card

        property string title: ""
        property string body: ""
        property bool stroked: false

        Layout.fillWidth: true
        implicitHeight: cardCol.implicitHeight + 22
        radius: Tokens.radiusCard
        color: Theme.surface
        border.width: stroked ? 2 : 1
        border.color: stroked ? Theme.primary : Theme.outline

        ColumnLayout {
            id: cardCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 13
            anchors.rightMargin: 13
            spacing: 3

            Label {
                text: card.title
                color: Theme.onSurface
                font.pixelSize: Tokens.textBase
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                text: card.body
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    body: [
        // ── Step 1 · Connect (live: scan → pick → pair) ─────────────────────
        ColumnLayout {
            visible: guide.step === 0
            spacing: Tokens.s6
            Layout.fillWidth: true

            Label {
                text: qsTr("Dish reaches a host PC over your local network. Both machines must be on the same Wi-Fi or LAN.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Label {
                    text: App.scanning ? qsTr("Scanning your network…")
                          : App.foundCount > 0
                              ? qsTr("%n satellite(s) found", "", App.foundCount)
                              : qsTr("No satellites found yet")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textBase
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Kit.OutlineButton {
                    text: App.scanning ? qsTr("Scanning…") : qsTr("Rescan")
                    enabled: !App.scanning
                    onClicked: App.startDiscovery()
                }
            }

            Kit.DishProgressBar {
                Layout.fillWidth: true
                visible: App.scanning
                indeterminate: true
            }

            // The live host list — tap Pair… to open the pairing sheet for
            // that satellite (the wizard analogue of ConnectionsPage FOUND).
            Rectangle {
                visible: App.discoveredServers.length > 0
                Layout.fillWidth: true
                implicitHeight: hostsColumn.implicitHeight
                radius: Tokens.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.outline
                clip: true

                ColumnLayout {
                    id: hostsColumn
                    anchors.left: parent.left
                    anchors.right: parent.right
                    spacing: 0

                    Repeater {
                        model: App.discoveredServers
                        delegate: ColumnLayout {
                            id: hostRow
                            required property int index
                            required property var modelData
                            readonly property string displayName:
                                modelData.name.length > 0 ? modelData.name : modelData.ip

                            Layout.fillWidth: true
                            spacing: 0

                            Rectangle {
                                visible: hostRow.index > 0
                                Layout.fillWidth: true
                                implicitHeight: 1
                                color: Theme.outline
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Layout.topMargin: Tokens.s4
                                Layout.bottomMargin: Tokens.s4
                                Layout.leftMargin: Tokens.s5
                                Layout.rightMargin: Tokens.s5
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
                                        text: hostRow.displayName
                                        color: Theme.onSurface
                                        font.pixelSize: Tokens.textBase
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: hostRow.modelData.ip + " · " + hostRow.modelData.source
                                        color: Theme.muted
                                        font.family: Tokens.monoFamily
                                        font.pixelSize: Tokens.textMeta
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                Kit.KitButton {
                                    text: qsTr("Pair…")
                                    onClicked: {
                                        guide.pendingHostId = hostRow.modelData.id;
                                        wizardPair.openFor(hostRow.modelData.id,
                                                           hostRow.displayName);
                                    }
                                }
                            }
                        }
                    }
                }
            }

            // Empty state (android: the install-Satellite block + GitHub
            // button, shown only while the list is empty).
            OptionCard {
                visible: App.discoveredServers.length === 0
                stroked: true
                title: qsTr("Don't see your PC?")
                body: qsTr("Install the free Satellite app on the PC you want to play on — satellites on your LAN appear here automatically, no IP to type in.")
            }
            Kit.OutlineButton {
                visible: App.discoveredServers.length === 0
                text: qsTr("Get Satellite for your PC")
                Layout.alignment: Qt.AlignLeft
                onClicked: App.openExternalUrl(guide.satelliteUrl)
            }

            OptionCard {
                visible: App.onlineCount > 0
                stroked: true
                title: qsTr("Connected")
                body: qsTr("%1 is online. Next: a controller.").arg(App.firstOnlineName)
            }
        },

        // ── Step 2 · Controller (live detected pads) ────────────────────────
        ColumnLayout {
            visible: guide.step === 1
            spacing: Tokens.s6
            Layout.fillWidth: true

            Label {
                text: qsTr("Physical controllers only — Windows has no on-screen pad. Connect one over USB or Bluetooth and it appears as a slot automatically.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            OptionCard {
                stroked: true
                title: App.slotCount > 0 ? qsTr("%n controller(s) detected", "", App.slotCount)
                                         : qsTr("Waiting for a controller")
                body: App.slotCount > 0
                      ? qsTr("They are listed below and on the Controllers page — bind one to a satellite and it starts streaming.")
                      : qsTr("Plug in an Xbox, PlayStation, or generic pad. It shows up here the moment Windows sees it.")
            }

            // The pads as Windows sees them right now (android DETECTING list).
            Repeater {
                model: App.slotModel
                delegate: RowLayout {
                    required property string name
                    required property string emulateName
                    required property string dotColor

                    Layout.fillWidth: true
                    Layout.leftMargin: Tokens.s2
                    spacing: Tokens.s4

                    Kit.StatusDot { token: dotColor }
                    Label {
                        text: name
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textSummary
                        elide: Text.ElideRight
                    }
                    Label {
                        text: emulateName
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            OptionCard {
                title: qsTr("Pick what the host sees")
                body: qsTr("Per slot, Emulate chooses what the pad appears as on the host — from the satellite's own catalog.")
            }
        },

        // ── Step 3 · Summary ────────────────────────────────────────────────
        ColumnLayout {
            visible: guide.step === 2
            spacing: Tokens.s6
            Layout.fillWidth: true

            Label {
                text: (App.onlineCount > 0
                       ? qsTr("%n satellite(s) online", "", App.onlineCount)
                       : qsTr("No satellite online yet"))
                      + " · "
                      + (App.slotCount > 0
                         ? qsTr("%n controller(s) ready", "", App.slotCount)
                         : qsTr("no controllers yet"))
                color: Theme.onSurface
                font.pixelSize: Tokens.textBase
                font.weight: Font.DemiBold
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: qsTr("Bind a controller to a satellite on the Controllers page and play. Re-run this guide any time from Settings, and see Help & FAQ for concepts and troubleshooting.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    ]
}
