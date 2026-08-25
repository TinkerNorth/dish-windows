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

    // Index-aligned with the wire values (0 Auto, 1 Xbox, 2 PS, 3 Nintendo), so
    // the picker maps by position and the C++ never sees a localized word.
    readonly property var deviceOptions: [qsTr("Auto"), qsTr("Xbox"),
                                          qsTr("PlayStation"), qsTr("Nintendo")]

    readonly property int pairedCount: page.countWhere(true)
    readonly property int foundCount: page.countWhere(false)

    function countWhere(paired) {
        var n = 0;
        for (var i = 0; i < page.hostRows.length; ++i)
            if (page.hostRows[i].paired === paired) n += 1;
        return n;
    }

    function refresh() { page.hostRows = App.moonlightHosts(); }

    Component.onCompleted: if (!App.moonlightScanning()) App.startMoonlightDiscovery()

    Connections {
        target: App
        function onMoonlightHostsChanged() { page.refresh(); }
        function onMoonlightPairingFinished(hostId, ok) {
            page.refresh();
            if (ok) {
                pairSheet.close();
                // A freshly paired host can be asked what it runs.
                App.refreshMoonlightApps(hostId);
            } else {
                pairSheet.rejected = true;
            }
        }
        function onMoonlightAppsChanged(hostId) {
            if (hostId === appSheet.hostId)
                appSheet.apps = App.moonlightApps();
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
                readonly property bool streaming: host.phase === "streaming"
                                                  || host.phase === "faltering"
                readonly property bool busy: host.phase === "pairing"
                                             || host.phase === "launching"
                                             || host.phase === "connecting"

                Layout.fillWidth: true

                Accessible.role: Accessible.ListItem
                Accessible.name: qsTr("%1, Moonlight host, %2")
                                     .arg(host.label).arg(page.phaseText(host.phase))

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
                        Kit.StatusDot {
                            token: page.phaseDot(host.phase)
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
                            text: page.phaseText(host.phase)
                            tone: page.phaseTone(host.phase)
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // ── Session settings, only once the host is paired ───────
                    Kit.Card {
                        visible: host.paired
                        filled: false
                        dense: true
                        Layout.fillWidth: true

                        contentItem: ColumnLayout {
                            spacing: Tokens.s4

                            Kit.Eyebrow { mutedTone: true; text: qsTr("Session") }

                            // App pick.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Tokens.s4

                                Label {
                                    text: qsTr("App")
                                    color: Theme.mutedStrong
                                    font.pixelSize: Tokens.textMeta
                                }
                                Label {
                                    text: host.modelData.appName.length > 0
                                          ? host.modelData.appName
                                          : qsTr("Host default")
                                    color: Theme.onSurface
                                    font.pixelSize: Tokens.textMeta
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Kit.DishButton {
                                    text: qsTr("Choose…")
                                    variant: Kit.DishButton.Outline
                                    size: Kit.DishButton.Small
                                    onClicked: appSheet.openFor(host.hostId, host.label)
                                }
                            }

                            // Emulated-device pick. Static list, so it renders
                            // without asking the host anything.
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: Tokens.s4

                                Label {
                                    text: qsTr("Emulate")
                                    color: Theme.mutedStrong
                                    font.pixelSize: Tokens.textMeta
                                }
                                Item { Layout.fillWidth: true }
                                // SegmentedControl is value-based, so the wire
                                // int is mapped to its label and back here.
                                Kit.SegmentedControl {
                                    options: page.deviceOptions
                                    value: page.deviceOptions[host.modelData.deviceType]
                                    small: true
                                    onPicked: function (option) {
                                        App.setMoonlightDeviceType(
                                            host.hostId, page.deviceOptions.indexOf(option));
                                    }
                                }
                            }
                        }
                    }

                    // ── Actions ─────────────────────────────────────────────
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Item { Layout.fillWidth: true }

                        Kit.DishButton {
                            visible: !host.paired
                            text: qsTr("Pair…")
                            variant: Kit.DishButton.Primary
                            enabled: !host.busy
                            onClicked: pairSheet.openFor(host.hostId, host.label)
                        }
                        Kit.DishButton {
                            visible: host.paired && !host.streaming
                            text: host.busy ? qsTr("Connecting…") : qsTr("Connect")
                            variant: Kit.DishButton.Primary
                            enabled: !host.busy
                            onClicked: App.connectMoonlightHost(host.hostId, "")
                        }
                        Kit.DishButton {
                            visible: host.streaming
                            text: qsTr("Disconnect")
                            variant: Kit.DishButton.Outline
                            onClicked: App.disconnectMoonlightHost(host.hostId)
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
                                    forgetItem.implicitWidth
                                    + hostMenu.leftPadding + hostMenu.rightPadding)
            color: Theme.surface
            border.width: 1
            border.color: Theme.outline
            radius: Tokens.radiusButton
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
        bodyText: qsTr("Its pairing is deleted — you’ll need the PIN again.")
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
            // A fresh 4-digit code per attempt, which is what the host expects
            // to be told.
            pairSheet.pin = String(Math.floor(1000 + Math.random() * 9000));
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

    // ---- App pick -----------------------------------------------------------
    Kit.ContentDialog {
        id: appSheet

        property string hostId: ""
        property string hostName: ""
        property var apps: []

        eyebrow: qsTr("App")
        heading: qsTr("What should %1 launch?").arg(appSheet.hostName)
        preferredWidth: 430
        rejectText: qsTr("Close")
        acceptEnabled: false

        function openFor(id, name) {
            appSheet.hostId = id;
            appSheet.hostName = name;
            appSheet.apps = [];
            App.refreshMoonlightApps(id);
            appSheet.open();
        }

        body: [
            Kit.LoadingSpinner {
                visible: appSheet.apps.length === 0
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: Tokens.s5
                Layout.bottomMargin: Tokens.s5
            },
            Label {
                visible: appSheet.apps.length === 0
                text: qsTr("Asking the host what it can run…")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            },
            Repeater {
                model: appSheet.apps
                delegate: Kit.RowButton {
                    id: appRow
                    required property var modelData
                    title: appRow.modelData.title.length > 0
                           ? appRow.modelData.title : appRow.modelData.id
                    Layout.fillWidth: true
                    onClicked: {
                        App.setMoonlightApp(appSheet.hostId, appRow.modelData.id,
                                            appRow.modelData.title);
                        appSheet.close();
                    }
                }
            }
        ]
    }

    // ---- Helpers: tokens to localized copy ----------------------------------
    function phaseText(token) {
        switch (token) {
        case "pairing":    return qsTr("Pairing…");
        case "paired":     return qsTr("Paired");
        case "launching":  return qsTr("Starting…");
        case "connecting": return qsTr("Connecting…");
        case "streaming":  return qsTr("Streaming");
        case "faltering":  return qsTr("Unsteady");
        case "failed":     return qsTr("Failed");
        case "closed":     return qsTr("Disconnected");
        default:           return qsTr("Found");
        }
    }
    function phaseTone(token) {
        switch (token) {
        case "streaming":  return Kit.CapabilityChip.Ok;
        case "paired":     return Kit.CapabilityChip.Present;
        case "faltering":  return Kit.CapabilityChip.Warn;
        case "connecting": return Kit.CapabilityChip.Warn;
        case "launching":  return Kit.CapabilityChip.Warn;
        case "pairing":    return Kit.CapabilityChip.Warn;
        case "failed":     return Kit.CapabilityChip.Warn;
        default:           return Kit.CapabilityChip.Neutral;
        }
    }
    function phaseDot(token) {
        switch (token) {
        case "streaming":  return "success";
        case "paired":     return "primary";
        case "faltering":  return "warning";
        case "failed":     return "warning";
        default:           return "muted";
        }
    }
}
