// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Home destination — the signal-path wiring diagram (design frame f-h1
// "Home — signal path"): one row per pad, pad → this PC → satellite, with the
// measured rate and one-way latency riding the live wire and the dish glyph at
// its center. An unbound pad dangles a dashed "Bind…" action card (frame f-h2's
// control vocabulary); the last row is the "+ Add a controller" invitation. The
// floating pill at the page foot carries the high-level state ("Streaming — do
// not close") while the display-sleep inhibitor is held.
//
// Every value binds the frozen App contract: the pad cell + wire read
// App.slotModel roles (rate chips via the same SlotLiveStats mapper as the
// Controllers cards), the satellite cell reads the sat* join roles (the same
// ConnectionRow vocabulary the Connections page renders), and the header counts
// are the shell primitives (streamingSlotCount / onlineCount). No business
// logic lives here — wording and colors only.

// Bound so delegates reference the outer `page` id and their `required` model
// props statically. `App` stays unqualified: a runtime context property the
// linter cannot resolve (the accepted limitation every page notes).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Home")

    // ---- Shell header contract (rendered by AppShell). Ladder mirrors the
    // sibling pages: fresh install → the getting-started nudge (muted);
    // anything streaming → the design's count line (success); satellites online
    // but nothing streaming (primary); else quiet (muted).
    readonly property string headerTitle: qsTr("Home")
    readonly property string headerSub: {
        if (App.slotCount === 0 && App.connectionCount === 0)
            return qsTr("Plug in a controller and pair a Satellite to get started");
        if (App.streamingSlotCount > 0)
            return page.controllersStreamingText(App.streamingSlotCount)
                   + " · " + page.satellitesOnlineText(App.onlineCount);
        if (App.onlineCount > 0)
            return page.satellitesOnlineText(App.onlineCount) + qsTr(" · nothing streaming");
        return qsTr("Nothing streaming");
    }
    readonly property string headerDot: {
        if (App.streamingSlotCount > 0)
            return "success";
        if (App.onlineCount > 0)
            return "primary";
        return "muted";
    }

    // The enclosing shell StackView (for the setup-guide hand-off), type-erased
    // through `var` so the dynamic `shellApi` resolves without lint warnings.
    readonly property var shellStack: StackView.view
    readonly property var shellApi: shellStack ? shellStack.shellApi : null

    // Signal-path geometry: the design fixes the pad/satellite nodes at 264px
    // with a ≥120px wire between; on a narrow pane the nodes give way first so
    // the wire never collapses.
    readonly property real wireMinWidth: 120
    readonly property real nodeWidth: {
        var w = contentItem ? contentItem.width : width;
        return Math.max(170, Math.min(264, (w - page.wireMinWidth - 2 * Tokens.s7) / 2));
    }

    // ---- Column eyebrows: Pad · This PC · Satellite -------------------------
    RowLayout {
        width: parent ? parent.width : implicitWidth
        spacing: 0

        Kit.Eyebrow { mutedTone: true; text: qsTr("Pad") }
        Item { Layout.fillWidth: true }
        Kit.Eyebrow { mutedTone: true; text: qsTr("This PC") }
        Item { Layout.fillWidth: true }
        Kit.Eyebrow { mutedTone: true; text: qsTr("Satellite") }
    }

    // ---- One row per pad ----------------------------------------------------
    ListView {
        id: pathList
        width: parent ? parent.width : implicitWidth
        height: contentHeight
        interactive: false
        spacing: Tokens.s6
        model: App.slotModel

        delegate: RowLayout {
            id: row

            // Slot roles this row consumes (contract §2 + the sat* join).
            required property string slotId
            required property string name
            required property bool bound
            required property string boundLabel
            required property bool live
            required property bool registering
            required property bool bluetooth
            required property string emulateName
            required property int batteryLevel
            required property int batteryStatus
            required property bool batteryKnown
            required property int gamepadHz
            required property bool gamepadHzLive
            required property bool gamepadHzShown
            required property string satIp
            required property string satLinkState
            required property string satChip
            required property string satDotColor
            required property string satGlyph
            required property string satLatencyText
            required property int satLatencySamples

            // Latency rides the label only while genuinely measured (the same
            // gate the Connections rows apply).
            readonly property bool showLatency:
                satLatencySamples > 0 && (satLinkState === "connected"
                                          || satChip === "unstable")

            width: ListView.view ? ListView.view.width : implicitWidth
            spacing: Tokens.s7

            // Pad node: name over the "as <type> · Battery <n>%" sub-line.
            Kit.Card {
                Layout.preferredWidth: page.nodeWidth
                Layout.alignment: Qt.AlignVCenter
                topPadding: 10
                bottomPadding: 10
                leftPadding: Tokens.s6
                rightPadding: Tokens.s6

                contentItem: ColumnLayout {
                    spacing: Tokens.s1

                    Label {
                        text: row.name
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Label {
                        text: page.padSubText(row)
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            // The wire: rate · latency over the line through the dish glyph.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: page.wireMinWidth
                Layout.alignment: Qt.AlignVCenter
                spacing: Tokens.s2

                Kit.LiveStat {
                    live: row.live
                    text: page.wireLabel(row)
                    Layout.alignment: Qt.AlignHCenter
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.s3

                    WireLine { dead: !row.live; Layout.fillWidth: true }
                    Kit.BrandGlyph {
                        glyph: row.live ? "dish-connected" : "dish-off"
                        Layout.preferredWidth: 20
                        Layout.preferredHeight: 20
                    }
                    WireLine { dead: !row.live; Layout.fillWidth: true }
                }
            }

            // Satellite node: the bound row's card, or the "Bind…" ghost.
            Kit.Card {
                visible: row.bound
                Layout.preferredWidth: page.nodeWidth
                Layout.alignment: Qt.AlignVCenter
                topPadding: 10
                bottomPadding: 10
                leftPadding: Tokens.s6
                rightPadding: Tokens.s6

                contentItem: RowLayout {
                    spacing: Tokens.s4

                    Kit.BrandGlyph {
                        glyph: glyphForToken(row.satGlyph)
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Kit.StatusDot {
                        token: row.satDotColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s1

                        Label {
                            text: row.boundLabel
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Kit.LiveStat {
                            text: row.satIp
                            Layout.fillWidth: true
                        }
                    }
                    Label {
                        text: page.chipText(row.satChip)
                        color: page.chipColor(row.satChip)
                        font.pixelSize: Tokens.textMeta
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }
            Kit.ActionCard {
                visible: !row.bound && !row.registering
                Layout.preferredWidth: page.nodeWidth
                Layout.alignment: Qt.AlignVCenter
                title: qsTr("Bind…")
                subtitle: qsTr("Choose a satellite for this pad")
                // Gated on the slot's filtered pick-list (contract §1); the
                // App.connectionCount read enlists the NOTIFY-less invokable
                // in the state graph, mirroring the Controllers bind button.
                enabled: App.connectionCount >= 0
                         && App.availableConnectionsForSlot(row.slotId).length > 0
                onClicked: bindDialog.openFor(row.slotId, row.name)
            }
            // A registering pad's satellite cell stays empty (nothing to bind
            // yet); keep the column so the wire geometry holds.
            Item {
                visible: !row.bound && row.registering
                Layout.preferredWidth: page.nodeWidth
            }
        }
    }

    // ---- The "+ Add a controller" invitation row ----------------------------
    RowLayout {
        width: parent ? parent.width : implicitWidth
        spacing: Tokens.s7

        Kit.ActionCard {
            Layout.preferredWidth: page.nodeWidth
            Layout.alignment: Qt.AlignVCenter
            showPlus: true
            title: qsTr("Add a controller")
            subtitle: qsTr("Wired, or Bluetooth")
            // The setup guide's Controller step explains exactly this (pads
            // auto-appear on Windows — no in-app add flow to run).
            onClicked: if (page.shellApi) page.shellApi.openSetupGuideAt(1)
        }
        WireLine {
            dead: true
            opacity: 0.5
            Layout.fillWidth: true
            Layout.minimumWidth: page.wireMinWidth
            Layout.alignment: Qt.AlignVCenter
        }
        Item { Layout.preferredWidth: page.nodeWidth }
    }

    // ---- Floating status pill (design: "Streaming — do not close") ----------
    // In the page footer so it holds while the path list scrolls; only shown
    // while the display-sleep inhibitor is held (the same keepAwakeActive gate
    // as the Controllers header pill).
    footer: Item {
        implicitHeight: App.keepAwakeActive ? pill.implicitHeight + Tokens.s8 : 0

        Rectangle {
            id: pill
            visible: App.keepAwakeActive
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            implicitWidth: pillRow.implicitWidth + 28
            implicitHeight: pillRow.implicitHeight + 12
            radius: height / 2
            color: Theme.primaryFill
            border.width: 1
            border.color: Theme.outline

            RowLayout {
                id: pillRow
                anchors.centerIn: parent
                spacing: Tokens.s4

                Kit.StatusDot { token: "success" }
                Text {
                    text: qsTr("Streaming — do not close")
                    color: Theme.onSurface
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                    font.letterSpacing: Tokens.sectionLetterSpacing
                    font.capitalization: Font.AllUppercase
                }
            }
        }
    }

    // The shared bind chooser (design FBindDlg), retargeted per ghost card.
    BindChooserDialog {
        id: bindDialog
    }

    // ---- The wire line: solid accent when live, dashed outline when dead ----
    component WireLine: Item {
        id: wire
        property bool dead: false
        implicitHeight: 2

        Rectangle {
            anchors.fill: parent
            visible: !wire.dead
            color: Theme.primary
        }
        Canvas {
            id: dashCanvas
            anchors.fill: parent
            visible: wire.dead
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = String(Theme.outline);
                ctx.lineWidth = 2;
                ctx.setLineDash([4, 4]);
                ctx.beginPath();
                ctx.moveTo(0, height / 2);
                ctx.lineTo(width, height / 2);
                ctx.stroke();
            }
            Connections {
                target: Theme
                function onPaletteChanged() { dashCanvas.requestPaint(); }
            }
        }
    }

    // ---- Helpers (presentation only; wording owned by this page) ------------

    // "2 controllers streaming" / "1 controller streaming". Explicit singular /
    // plural pairs (no %n): the app ships with English fallback catalogs, and
    // untranslated %n would render its "(s)" literally.
    function controllersStreamingText(n) {
        return n === 1 ? qsTr("1 controller streaming")
                       : qsTr("%1 controllers streaming").arg(n);
    }
    function satellitesOnlineText(n) {
        return n === 1 ? qsTr("1 satellite online")
                       : qsTr("%1 satellites online").arg(n);
    }

    // The pad card's sub-line: "as DualShock 4 · Battery 82%" bound,
    // "Unbound · wired" loose, the busy line while attaching. A Bluetooth pad
    // names its transport instead of "wired" — and suppresses the status-4
    // battery word, which is the HOST-battery substitute (a desktop reads
    // 100%/WIRED when the pad's own charge is unknown) and would contradict
    // the wireless link.
    function padSubText(row) {
        if (row.registering)
            return qsTr("Registering controller…");
        if (!row.bound) {
            if (row.bluetooth)
                return qsTr("Unbound · Bluetooth");
            return row.batteryKnown && row.batteryStatus === 4
                   ? qsTr("Unbound · wired") : qsTr("Unbound");
        }
        var parts = [];
        if (row.emulateName.length > 0)
            parts.push(qsTr("as %1").arg(row.emulateName));
        if (row.bluetooth)
            parts.push(qsTr("Bluetooth"));
        if (row.batteryKnown && !(row.bluetooth && row.batteryStatus === 4))
            parts.push(page.batteryText(row.batteryLevel, row.batteryStatus));
        return parts.length > 0 ? parts.join(" · ") : qsTr("Bound");
    }

    // Battery wording — identical to the Controllers chip (contract §2:
    // 2=charging, 3=full, 4=wired; batteryKnown gates 255 out).
    function batteryText(level, status) {
        if (status === 4) { return qsTr("Battery wired"); }
        if (status === 2) { return qsTr("Battery %1% ↑").arg(level); }
        if (status === 3) { return qsTr("Battery full"); }
        return qsTr("Battery %1%").arg(level);
    }

    // The wire's mono label: "250 Hz · ~3.4 ms" on a live link (whichever
    // halves are measured), "idle" on a dead one.
    function wireLabel(row) {
        if (!row.live)
            return qsTr("idle");
        var parts = [];
        if (row.gamepadHzShown) {
            parts.push(row.gamepadHzLive ? qsTr("%1 Hz").arg(row.gamepadHz)
                                         : qsTr("~%1 Hz").arg(row.gamepadHz));
        }
        if (row.showLatency)
            parts.push(row.satLatencyText);
        return parts.length > 0 ? parts.join(" · ") : qsTr("live");
    }

    // Localized chip text/tone for the satellite cell — the same ladders the
    // Connections page renders from the identical tokens.
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
}
