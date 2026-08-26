// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Moonlight destination — the inventory of GameStream HOSTS (Sunshine,
// Apollo, Wolf), the sibling of ConnectionsPage's satellite inventory. It is
// deliberately a SEPARATE page rather than a second section of Connections: the
// two host kinds pair differently, carry different capabilities, and a merged
// list would make "Pair" mean two different things in one column.
//
// Every row states its kind in words ("Moonlight host"), because the only visual
// difference from a satellite row would otherwise be the glyph.
//
// THIS SCREEN OWNS TRUST, NOT SESSIONS. Moonlight has no bidirectional liveness:
// pairing is one-time trust that only we can check, the host never notifies us,
// and a host-side unpair is discovered on the next call. So a row carries a trust
// WORD verified when the screen is entered, never a connection light. Which
// controller drives which host, what it emulates and what the host runs are all
// questions the binding flow asks, because a session belongs to a host and its
// controllers rather than to this inventory.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Moonlight hosts")

    readonly property string headerTitle: qsTr("Moonlight hosts")
    readonly property string headerSub: page.pairedCount === 0
        ? qsTr("%n found", "", page.foundCount)
        : qsTr("%n paired", "", page.pairedCount)
    readonly property string headerDot: page.pairedCount === 0 ? "muted" : "success"

    // Where a user without a host goes to get one.
    readonly property string sunshineUrl: "https://github.com/LizardByte/Sunshine"

    property var hostRows: App.moonlightHosts()
    property string currentHostId: ""
    property string currentLabel: ""
    // Bumped on every host move so the invokable-backed trust and controller
    // counts re-evaluate: a call is not a binding dependency.
    property int accounting: 0

    readonly property int pairedCount: page.countWhere(true)
    readonly property int foundCount: page.countWhere(false)

    function countWhere(paired) {
        var n = 0;
        for (var i = 0; i < page.hostRows.length; ++i)
            if (page.hostRows[i].paired === paired) n += 1;
        return n;
    }

    function refresh() {
        page.hostRows = App.moonlightHosts();
        page.accounting += 1;
    }

    // Remembered trust is re-verified on entering the screen and never polled.
    function reprobe() {
        for (var i = 0; i < page.hostRows.length; ++i)
            App.probeMoonlightHost(page.hostRows[i].id);
    }

    Component.onCompleted: {
        if (!App.moonlightScanning())
            App.startMoonlightDiscovery();
        page.reprobe();
    }

    Connections {
        target: App
        function onMoonlightHostsChanged() { page.refresh(); }
        function onMoonlightPairingFinished(hostId, ok) {
            page.refresh();
            if (ok) {
                pairSheet.close();
            } else {
                pairSheet.rejected = true;
            }
        }
    }

    ColumnLayout {
        width: parent.width
        spacing: Tokens.s5

        // ---- FOUND + manual add ---------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.s4

            Kit.SectionHeader { glyph: "dish-logo"; label: qsTr("Found") }
            Item { Layout.fillWidth: true }
            Kit.LiveStat {
                live: App.moonlightScanning()
                text: App.moonlightScanning() ? qsTr("scanning…")
                                              : qsTr("%n found", "", page.foundCount)
            }
            Kit.DishButton {
                text: qsTr("Add by address…")
                variant: Kit.DishButton.Outline
                onClicked: addSheet.open()
            }
            Kit.DishButton {
                text: App.moonlightScanning() ? qsTr("Scanning…") : qsTr("Scan")
                variant: Kit.DishButton.Outline
                enabled: !App.moonlightScanning()
                onClicked: App.startMoonlightDiscovery()
            }
        }

        Kit.DishProgressBar {
            visible: App.moonlightScanning()
            indeterminate: true
            Layout.fillWidth: true
        }

        // Empty is a real state, and it says so differently while a sweep runs.
        Kit.EmptyState {
            visible: page.hostRows.length === 0 && App.moonlightScanning()
            glyph: "satellite-broadcasting"
            title: qsTr("Looking for Moonlight hosts")
            body: qsTr("Scanning your network for hosts advertising GameStream. They appear here as they answer.")
            showAction: false
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s5
            Layout.bottomMargin: Tokens.s5
        }

        Kit.EmptyState {
            visible: page.hostRows.length === 0 && !App.moonlightScanning()
            glyph: "satellite-off"
            title: qsTr("No Moonlight hosts found")
            body: qsTr("A PC shows up here once Sunshine, Apollo or Wolf is running on it and both machines are on the same network. You can also add one by address.")
            showAction: false
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s5
            Layout.bottomMargin: Tokens.s5

            RowLayout {
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Tokens.s3
                spacing: Tokens.s4

                Kit.DishButton {
                    text: qsTr("Get Sunshine ↗")
                    variant: Kit.DishButton.Outline
                    onClicked: App.openExternalUrl(page.sunshineUrl)
                }
                Kit.DishButton {
                    text: qsTr("Add by address…")
                    variant: Kit.DishButton.Outline
                    onClicked: addSheet.open()
                }
            }
        }

        // ---- One card per host ----------------------------------------------
        Repeater {
            model: page.hostRows

            delegate: Kit.Card {
                id: host
                required property var modelData

                readonly property string hostId: host.modelData.id
                readonly property string label: host.modelData.name
                readonly property bool paired: host.modelData.paired
                readonly property string phase: host.modelData.phase
                readonly property bool busy: host.phase === "pairing"

                readonly property string trust: page.accounting >= 0
                                                ? App.moonlightTrust(host.hostId) : ""
                readonly property int controllers: page.accounting >= 0
                                                   ? App.moonlightBoundSlotCount(host.hostId) : 0

                Layout.fillWidth: true

                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, Moonlight host, %2")
                                     .arg(host.label).arg(page.trustText(host.trust))

                contentItem: ColumnLayout {
                    spacing: Tokens.s5

                    // ── Header: glyph, dot, name, address, chip ──────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Kit.BrandGlyph {
                            glyph: "dish-logo"
                            Layout.preferredWidth: Tokens.glyphSm
                            Layout.preferredHeight: Tokens.glyphSm
                            Layout.alignment: Qt.AlignVCenter
                        }
                        ColumnLayout {
                            spacing: Tokens.s0
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter

                            Label {
                                text: host.label
                                color: Theme.onSurface
                                font.pixelSize: Tokens.textBase
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            // The kind, in words: a glyph alone would not say it.
                            Label {
                                text: qsTr("Moonlight host (Sunshine/Apollo)")
                                color: Theme.muted
                                font.pixelSize: Tokens.textMeta
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                        }
                        Kit.LiveStat {
                            text: host.modelData.ip
                            elide: Text.ElideRight
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Kit.CapabilityChip {
                            text: page.trustText(host.trust)
                            tone: page.trustTone(host.trust)
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Kit.CapabilityChip {
                            visible: host.controllers > 0
                            text: qsTr("In use by %1")
                                    .arg(qsTr("%n controllers", "", host.controllers))
                            tone: Kit.CapabilityChip.Ok
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // What a controller bound to this host will run, stated but
                    // never chosen here: the app belongs to the session, and the
                    // session belongs to the binding flow.
                    Label {
                        visible: host.paired
                        text: host.modelData.appName.length > 0
                              ? qsTr("Session · %1").arg(host.modelData.appName)
                              : qsTr("Session · whatever the host lists first")
                        color: Theme.mutedStrong
                        font.pixelSize: Tokens.textMeta
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }

                    // ── Actions ─────────────────────────────────────────────
                    // Pair, Forget and one escape hatch. Connecting is implicit:
                    // binding a controller starts or joins the session and
                    // unbinding the last one tears it down.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Item { Layout.fillWidth: true }

                        Kit.DishButton {
                            visible: host.trust !== "paired"
                            text: qsTr("Pair…")
                            variant: Kit.DishButton.Primary
                            enabled: !host.busy
                            onClicked: pairSheet.openFor(host.hostId, host.label)
                        }
                        Kit.DishButton {
                            text: "⋯"
                            variant: Kit.DishButton.Outline
                            Accessible.name: qsTr("More actions for %1").arg(host.label)
                            onClicked: {
                                page.currentHostId = host.hostId;
                                page.currentLabel = host.label;
                                hostMenu.popup();
                            }
                        }
                    }
                }
            }
        }
    }

    // ---- Host overflow ------------------------------------------------------
    Menu {
        id: hostMenu

        background: Rectangle {
            implicitWidth: Math.max(Tokens.menuMinWidth,
                                    Math.max(forgetItem.implicitWidth, quitItem.implicitWidth)
                                    + hostMenu.leftPadding + hostMenu.rightPadding)
            color: Theme.surface
            border.width: 1
            border.color: Theme.outline
            radius: Tokens.radiusButton
        }

        // The escape hatch for a host that is running something nobody here can
        // reach. /cancel answers 200 whether or not anything was running, so the
        // host is asked again afterwards rather than believed.
        MenuItem {
            id: quitItem
            text: qsTr("Quit session")

            contentItem: Text {
                text: quitItem.text
                font.pixelSize: Tokens.textSummary
                color: Theme.onSurface
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: quitItem.highlighted ? Theme.primaryHover : "transparent"
                radius: Tokens.radiusChip
            }
            onTriggered: App.quitMoonlightApp(page.currentHostId)
        }

        MenuItem {
            id: forgetItem
            text: qsTr("Forget")

            contentItem: Text {
                text: forgetItem.text
                font.pixelSize: Tokens.textSummary
                color: Theme.error
                verticalAlignment: Text.AlignVCenter
            }
            background: Rectangle {
                color: forgetItem.highlighted ? Theme.primaryHover : "transparent"
                radius: Tokens.radiusChip
            }
            onTriggered: forgetConfirm.open()
        }
    }

    Kit.ConfirmDialog {
        id: forgetConfirm
        eyebrow: qsTr("Forget")
        heading: qsTr("Forget %1?").arg(page.currentLabel)
        bodyText: qsTr("Its pairing is deleted. You will need the PIN again.")
        acceptText: qsTr("Forget")
        rejectText: qsTr("Cancel")
        destructiveAccept: true
        onAccepted: {
            App.forgetMoonlightHost(page.currentHostId);
            forgetConfirm.close();
        }
    }

    // ---- Add by address -----------------------------------------------------
    // The discovery fallback: mDNS does not cross every subnet, so a host can
    // always be reached by typing where it lives.
    Kit.ContentDialog {
        id: addSheet
        eyebrow: qsTr("Moonlight host")
        heading: qsTr("Add a host by address")
        preferredWidth: 420
        acceptText: qsTr("Add")
        rejectText: qsTr("Cancel")
        acceptEnabled: addressField.text.trim().length > 0

        body: [
            Label {
                text: qsTr("Enter the host’s IP address or hostname. Dish uses the standard Moonlight ports.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: addressField
                placeholderText: qsTr("192.168.1.20")
                Layout.fillWidth: true
            },
            Kit.KitTextField {
                id: nameField
                placeholderText: qsTr("Name (optional)")
                Layout.fillWidth: true
            }
        ]

        onAccepted: {
            App.addMoonlightHost(addressField.text.trim(), nameField.text.trim());
            addressField.clear();
            nameField.clear();
            addSheet.close();
        }
        onClosed: { addressField.clear(); nameField.clear(); }
    }

    // ---- Pairing ------------------------------------------------------------
    // Moonlight pairing is the mirror of the satellite's: the PIN is generated
    // HERE and typed into the host's web UI, so this sheet DISPLAYS a code
    // rather than asking for one.
    Kit.ContentDialog {
        id: pairSheet

        property string hostId: ""
        property string hostName: ""
        property string pin: ""
        property bool rejected: false

        eyebrow: qsTr("Pairing")
        heading: qsTr("Pair with %1").arg(pairSheet.hostName)
        preferredWidth: 430
        rejectText: qsTr("Cancel")
        acceptText: qsTr("Done")
        acceptEnabled: false

        function openFor(id, name) {
            pairSheet.hostId = id;
            pairSheet.hostName = name;
            pairSheet.rejected = false;
            // A fresh 4-digit code per attempt, which is what the host expects to
            // be told. Minted in C++: a PIN is security-relevant and JavaScript's
            // Math.random() is not a suitable source.
            pairSheet.pin = App.moonlightPairingPin();
            App.pairMoonlightHost(id, pairSheet.pin);
            pairSheet.open();
        }

        body: [
            Label {
                textFormat: Text.StyledText
                text: qsTr("Type this PIN into the <b>%1</b> web interface to approve the pairing.")
                          .arg(pairSheet.hostName)
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            RowLayout {
                spacing: Tokens.s4
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Tokens.s3
                Layout.bottomMargin: Tokens.s3

                Repeater {
                    model: 4
                    delegate: Rectangle {
                        id: pinCell
                        required property int index

                        implicitWidth: 40
                        implicitHeight: 48
                        radius: Tokens.radiusButton
                        color: Theme.surfaceDim
                        border.width: 1
                        border.color: pairSheet.rejected ? Theme.error : Theme.outline

                        Label {
                            anchors.centerIn: parent
                            text: pinCell.index < pairSheet.pin.length
                                  ? pairSheet.pin.charAt(pinCell.index) : ""
                            color: Theme.primary
                            font.family: Tokens.monoFamily
                            font.pixelSize: Tokens.textHero
                        }
                    }
                }
            },
            RowLayout {
                spacing: Tokens.s5
                Layout.fillWidth: true

                Kit.DishProgressBar {
                    visible: !pairSheet.rejected
                    indeterminate: true
                    Layout.preferredWidth: 60
                }
                Label {
                    text: pairSheet.rejected
                          ? qsTr("The host did not accept the pairing. Try again.")
                          : qsTr("Waiting for approval on the host…")
                    color: pairSheet.rejected ? Theme.error : Theme.muted
                    font.pixelSize: Tokens.textSummary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Kit.DishButton {
                    visible: pairSheet.rejected
                    text: qsTr("New code")
                    variant: Kit.DishButton.Outline
                    size: Kit.DishButton.Small
                    onClicked: pairSheet.openFor(pairSheet.hostId, pairSheet.hostName)
                }
            }
        ]
    }

    // ---- Helpers: tokens to localized copy ----------------------------------
    // Three words and nothing else. "Remembered" is not a failure and is not
    // amber: a host that is switched off is not a problem to solve.
    function trustText(token) {
        switch (token) {
        case "paired":     return qsTr("Paired");
        case "remembered": return qsTr("Remembered");
        }
        return qsTr("Not paired");
    }
    function trustTone(token) {
        if (token === "paired")
            return Kit.CapabilityChip.Ok;
        if (token === "remembered")
            return Kit.CapabilityChip.Neutral;
        return Kit.CapabilityChip.Absent;
    }
}
