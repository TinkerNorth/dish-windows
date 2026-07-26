// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The 3-step setup guide as an in-window dialog over the shell (design frame
// f-a2): Connect (scan + PIN) · Controller (pads auto-appear; no on-screen pad
// on Windows) · Summary. Informational option cards per the design, with live
// state (found/online/slot counts) folded into the copy so the guide reflects
// reality as the user follows it. Opened from the Settings "Setup guide" row
// and the welcome hand-off; closing is always allowed (re-run any time).

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.ContentDialog {
    id: guide

    property int step: 0
    readonly property int stepCount: 3

    eyebrow: qsTr("Setup guide · step %1 of %2").arg(step + 1).arg(stepCount)
    heading: step === 0 ? qsTr("Find and pair your Satellite")
           : step === 1 ? qsTr("Plug in a controller")
           : qsTr("You're set")
    acceptText: step < stepCount - 1 ? qsTr("Next") : qsTr("Finish")
    rejectText: qsTr("Close")
    preferredWidth: 470

    onOpened: step = 0
    onAccepted: {
        if (step < stepCount - 1)
            step += 1;
        else
            close();
    }

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
        // ── Step 1 · Connect ────────────────────────────────────────────────
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
            OptionCard {
                stroked: true
                title: qsTr("Find your PC on the network")
                body: App.foundCount > 0
                      ? qsTr("Open Connections and hit Scan. %n satellite(s) are already visible on your LAN.", "", App.foundCount)
                      : qsTr("Open Connections and hit Scan. Satellites running on your LAN appear automatically — no IP to type in.")
            }
            OptionCard {
                title: qsTr("Enter the operator PIN")
                body: qsTr("Pick your Satellite and enter the 6-digit PIN it shows on the host screen. Once accepted, the pairing is remembered.")
            }
        },

        // ── Step 2 · Controller ─────────────────────────────────────────────
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
                      ? qsTr("They are listed on the Controllers page — bind one to a satellite and it starts streaming.")
                      : qsTr("Plug in an Xbox, PlayStation, or generic pad. It shows up on the Controllers page the moment Windows sees it.")
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
