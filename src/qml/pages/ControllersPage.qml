// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Controllers destination — the inventory of pads (design v3 "S09 ·
// Controllers"): everything that belongs to the DEVICE, nothing that belongs to
// a binding. `Bind…`, `Emulate…` and `Unbind` are gone; all three write a
// binding, so all three live on Home and in the setup wizard. The card's one
// binding-aware element is the `Bound · <host> ›` readout that hands off to the
// binding editor — an unbound pad prints a mono `not bound` and offers no
// control at all.
//
// Two v3 rules shape the content. Absence is DRAWN, never merely missing: a pad
// with no gyro renders the outlined `No gyro` chip at full opacity rather than
// nothing. And colour carries live-vs-idle only — the `~` marks a derived
// figure, so an estimated rate on a live wire still reads live.
//
// Every value binds the frozen App contract; no business logic lives here.

// Bound so the slot delegate references the outer `page` id and its `required`
// model props statically. `App` stays unqualified: a runtime context property
// the linter cannot resolve (the accepted limitation every page notes).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Controllers")

    // ---- Shell header contract (rendered by AppShell, not by this body).
    // Counts come from the one shared surface so two headers can never
    // disagree; this page owns only the wording.
    readonly property string headerTitle: qsTr("Controllers")
    // Two counts, so each half is its own %n message joined by the shared "·".
    readonly property string headerSub: qsTr("%n connected", "", App.slotCount)
                                        + " · " + qsTr("%n bound", "", App.boundSlotCount)
    readonly property string headerDot: App.slotCount > 0 ? "success" : "muted"

    // The enclosing shell StackView, type-erased through `var` so the shell's
    // dynamic `shellApi` resolves without lint warnings.
    readonly property var shellStack: StackView.view
    readonly property var shellApi: shellStack ? shellStack.shellApi : null

    // At or under this charge the battery pill turns amber (the design draws
    // 18 % as the low case).
    readonly property int batteryLowLevel: 20

    // LiveStat owns the rate formatter — nothing else in the app formats a
    // rate — so the composed meta line borrows this instance.
    Kit.LiveStat { id: rateFormat; visible: false }

    // ---- Empty state --------------------------------------------------------
    // Centred in the content viewport: the wrapper spans the ScrollView's
    // height, and with the list hidden it is the only laid-out child.
    Item {
        visible: App.slotCount === 0
        width: parent ? parent.width : implicitWidth
        height: page.contentItem.height

        Kit.EmptyState {
            anchors.centerIn: parent
            width: parent.width
            glyph: "dish-off"
            title: qsTr("No controllers connected")
            body: qsTr("Plug in an Xbox, PlayStation, or generic pad over USB or Bluetooth — Windows detects it and Dish lists it here automatically.")
            actionText: qsTr("Open Connections")
            showAction: true
            // Destination 2 is the Connections rail entry (AppShell order —
            // Home / Controllers / Connections).
            onActionRequested: if (page.shellApi) page.shellApi.selectDestination(2)
        }
    }

    ColumnLayout {
        id: bodyColumn
        visible: App.slotCount > 0
        width: parent.width
        spacing: Tokens.s5

        // ---- CONNECTED ------------------------------------------------------
        RowLayout {
            Layout.fillWidth: true
            spacing: Tokens.s4

            Kit.SectionHeader { label: qsTr("Connected") }
            Item { Layout.fillWidth: true }
            Kit.LiveStat { text: qsTr("%n connected", "", App.slotCount) }
        }

        // One card per attached pad. The list is content-sized and inert: the
        // page's single scroller owns all overflow, and the model's scoped
        // dataChanged keeps a moving Hz value from resetting the view.
        ListView {
            id: slotList
            Layout.fillWidth: true
            Layout.preferredHeight: contentHeight
            interactive: false
            spacing: Tokens.s5
            model: App.slotModel

            delegate: Kit.Card {
                id: card
                width: ListView.view ? ListView.view.width : implicitWidth

                // Slot roles this card consumes (contract §2 + §7.2).
                required property string slotId
                required property string name
                required property bool bound
                required property string boundLabel
                required property bool registering
                required property bool live
                required property string dotColor
                required property bool bluetooth
                required property bool remappable
                required property bool hasMotion
                required property bool hasTouchpad
                required property bool hasLightbar
                required property int batteryLevel
                required property int batteryStatus
                required property bool batteryKnown
                required property int gamepadHz
                required property bool gamepadHzLive
                required property bool gamepadHzShown
                required property int motionHz
                required property bool motionHzShown
                required property int pollHz
                required property bool pollHzShown
                // USB input-path roles. The two segments reflect these; picking
                // one forwards the wire token to App.setSlotPath.
                required property string pathPhase
                required property string desiredPath
                required property bool pathSupported
                required property bool claimInProgress
                required property string directFailure

                readonly property string pathNote:
                    pathSupported ? page.pathNoteText(pathPhase, directFailure) : ""

                Accessible.role: Accessible.ListItem
                Accessible.name: card.bound
                                 ? qsTr("%1, bound to %2").arg(card.name).arg(card.boundLabel)
                                 : qsTr("%1, not bound").arg(card.name)

                // The bands are evenly spaced by the layout, and an invisible
                // band costs nothing: a Qt layout skips hidden items and their
                // spacing, so the registering variant really does replace the
                // card rather than leaving a gap where it was.
                contentItem: ColumnLayout {
                    spacing: Tokens.s5

                    // ── Registering: the busy variant replaces every band ────
                    RowLayout {
                        visible: card.registering
                        Layout.fillWidth: true
                        spacing: Tokens.s6

                        Kit.BrandGlyph {
                            // The shipped `*-animated` files carry no <animate>
                            // element, so the transient is the bar below, not
                            // the glyph.
                            glyph: "dish"
                            Layout.preferredWidth: Tokens.glyphLg
                            Layout.preferredHeight: Tokens.glyphLg
                            Layout.alignment: Qt.AlignVCenter
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Tokens.s1

                            Label {
                                text: card.name
                                color: Theme.onSurface
                                font.pixelSize: Tokens.textBase
                                font.weight: Font.DemiBold
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: qsTr("Registering controller…")
                                color: Theme.mutedStrong
                                font.pixelSize: Tokens.textMeta
                            }
                        }
                    }
                    Kit.DishProgressBar {
                        visible: card.registering
                        indeterminate: true
                        Layout.fillWidth: true
                    }

                    // ── Band 1: dot · name · meta · binding readout ──────────
                    RowLayout {
                        visible: !card.registering
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Kit.StatusDot {
                            token: card.dotColor
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Label {
                            text: card.name
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                        // The name owns the row's slack, so a long device name
                        // elides instead of shoving the readout off the card.
                        Kit.LiveStat {
                            live: card.live
                            text: page.metaText(card)
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // The card's ONLY binding-aware control: the hand-off
                        // into the binding editor.
                        Kit.DishButton {
                            visible: card.bound
                            text: qsTr("Bound · %1 ›").arg(card.boundLabel)
                            variant: Kit.DishButton.Outline
                            size: Kit.DishButton.Small
                            Layout.alignment: Qt.AlignVCenter
                            onClicked: page.openBindingEditor(card.slotId)
                        }
                        // Unbound pads have no such row — you bind from Home.
                        Kit.LiveStat {
                            visible: !card.bound
                            text: qsTr("not bound")
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // ── Band 2: capability chips + battery + rate readouts ───
                    Flow {
                        visible: !card.registering
                        Layout.fillWidth: true
                        spacing: Tokens.s3

                        // Absence is drawn: the negated chip renders outlined at
                        // full opacity, never hidden.
                        Kit.CapabilityChip {
                            text: card.hasMotion ? qsTr("Gyro") : qsTr("No gyro")
                            tone: card.hasMotion ? Kit.CapabilityChip.Present
                                                 : Kit.CapabilityChip.Absent
                        }
                        Kit.CapabilityChip {
                            text: card.hasTouchpad ? qsTr("Touchpad") : qsTr("No touchpad")
                            tone: card.hasTouchpad ? Kit.CapabilityChip.Present
                                                   : Kit.CapabilityChip.Absent
                        }
                        // Lightbar is named only when the pad has an RGB LED —
                        // "no lightbar" is the normal case, not a finding.
                        Kit.CapabilityChip {
                            visible: card.hasLightbar
                            text: qsTr("Lightbar")
                            tone: Kit.CapabilityChip.Present
                        }
                        // Battery only once a real reading landed (255 is
                        // unknown — that is what batteryKnown is for). On a
                        // Bluetooth pad the status-4 reading is the HOST battery
                        // substitute and would contradict the wireless link.
                        Kit.CapabilityChip {
                            visible: card.batteryKnown
                                     && !(card.bluetooth && card.batteryStatus === 4)
                            text: page.batteryLabel(card.batteryLevel, card.batteryStatus)
                            tone: page.batteryLow(card.batteryLevel, card.batteryStatus)
                                  ? Kit.CapabilityChip.Low : Kit.CapabilityChip.Neutral
                        }

                        // Rate readouts. Colour carries live-vs-idle; the `~`
                        // inside the text carries measured-vs-derived.
                        Kit.LiveStat {
                            visible: card.motionHzShown
                            live: card.live
                            text: qsTr("Motion %1 Hz").arg(card.motionHz)
                        }
                        Kit.LiveStat {
                            visible: card.pollHzShown
                            live: card.live && card.pathPhase === "direct"
                            text: qsTr("Poll %1 Hz").arg(card.pollHz)
                        }
                    }

                    // ── Band 3: device actions · the input-path control ──────
                    RowLayout {
                        visible: !card.registering
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Kit.DishButton {
                            text: qsTr("Dead zones…")
                            variant: Kit.DishButton.Outline
                            onClicked: page.openDeadzones()
                        }
                        // Raw-DirectInput pads only: slotRemap() returns {} for
                        // anything SDL already maps.
                        Kit.DishButton {
                            visible: card.remappable
                            text: qsTr("Remap…")
                            variant: Kit.DishButton.Outline
                            onClicked: page.openRemap(card.slotId, card.name)
                        }

                        Item { Layout.fillWidth: true }

                        // Hidden entirely — never disabled — for a pad the
                        // raw-HID path cannot claim; a Bluetooth pad is never
                        // path-supported.
                        Kit.Eyebrow {
                            visible: card.pathSupported
                            mutedTone: true
                            text: qsTr("Input path")
                            Layout.alignment: Qt.AlignVCenter
                        }
                        // Two segments only: `auto` is a setSlotPath INPUT and
                        // never appears in desiredPath, so it is never offered.
                        Kit.SegmentedControl {
                            visible: card.pathSupported
                            small: true
                            busy: card.claimInProgress
                            options: [qsTr("Standard"), qsTr("Direct")]
                            value: card.desiredPath === "direct" ? qsTr("Direct")
                                                                 : qsTr("Standard")
                            Layout.alignment: Qt.AlignVCenter
                            onPicked: function(option) {
                                App.setSlotPath(card.slotId,
                                                option === qsTr("Direct") ? "direct"
                                                                          : "standard");
                            }
                        }
                    }

                    // ── Band 4: what the path is doing, in words ─────────────
                    // Every path failure is an inline reason. Never a toast.
                    RowLayout {
                        visible: !card.registering && card.pathSupported
                                 && (card.claimInProgress || card.pathNote.length > 0)
                        Layout.fillWidth: true
                        spacing: Tokens.s4

                        Kit.DishProgressBar {
                            visible: card.claimInProgress
                            indeterminate: true
                            Layout.preferredWidth: Tokens.s11 * 2
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Label {
                            visible: card.claimInProgress
                            text: qsTr("Claiming controller…")
                            color: Theme.mutedStrong
                            font.pixelSize: Tokens.textMeta
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Label {
                            visible: !card.claimInProgress && card.pathNote.length > 0
                            text: card.pathNote
                            color: page.pathNoteIsError(card.pathPhase, card.directFailure)
                                   ? Theme.error : Theme.warning
                            font.pixelSize: Tokens.textMeta
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }
                }
            }
        }
    }

    // ---- Telemetry footer ---------------------------------------------------
    // Pinned outside the scroller so it holds while the slot list moves.
    footer: Item {
        implicitHeight: telemetryRow.implicitHeight + Tokens.s8

        RowLayout {
            id: telemetryRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.leftMargin: Tokens.pagePadding
            anchors.rightMargin: Tokens.pagePadding
            spacing: Tokens.s9

            Kit.LiveStat {
                text: qsTr("%n connected", "", App.slotCount)
                      + " · " + qsTr("%n bound", "", App.boundSlotCount)
            }
            Kit.LiveStat {
                text: qsTr("events/s %1   sends/s %2")
                          .arg(App.eventsPerSec).arg(App.sendsPerSec)
            }
            Item { Layout.fillWidth: true }
            Kit.LiveStat {
                text: qsTr("total %1").arg(App.totalSent)
            }
        }
    }

    // ---- Navigation ---------------------------------------------------------

    function openBindingEditor(slotId) {
        if (!page.shellApi)
            return;
        page.shellApi.pushDetail(Qt.resolvedUrl("ConfigureBindingPage.qml"),
                                 qsTr("Configure binding"), { slotId: slotId });
    }

    // The raw-joystick remap detail. It needs slotId / slotName BEFORE load,
    // which is exactly what pushDetail's initial-property map is for.
    function openRemap(slotId, slotName) {
        if (!page.shellApi)
            return;
        page.shellApi.pushDetail(Qt.resolvedUrl("ControlsRemapPage.qml"),
                                 qsTr("Configure controls"),
                                 { slotId: slotId, slotName: slotName });
    }

    function openDeadzones() {
        if (!page.shellApi)
            return;
        page.shellApi.pushDetail(Qt.resolvedUrl("DeadzoneSettingsPage.qml"),
                                 qsTr("Dead zones & motion"), {});
    }

    // ---- Helpers (presentation only; wording owned by this page) ------------

    // The mono meta beside the name: transport, then the report rate when one
    // is known. `~` marks a derived figure; the colour is live-vs-idle.
    function metaText(row) {
        var parts = [];
        parts.push(row.bluetooth ? qsTr("Bluetooth") : qsTr("USB"));
        if (row.gamepadHzShown) {
            var rate = rateFormat.rateText(row.gamepadHz, row.gamepadHzLive);
            if (rate.length > 0)
                parts.push(rate);
        }
        return parts.join(" · ");
    }

    // Battery wording (contract §2: 2=charging, 3=full, 4=wired; batteryKnown
    // already gates the unknown 255 out).
    function batteryLabel(level, status) {
        if (status === 4) { return qsTr("Battery wired"); }
        if (status === 2) { return qsTr("Battery %1% ↑").arg(level); }
        if (status === 3) { return qsTr("Battery full"); }
        return qsTr("Battery %1%").arg(level);
    }
    // Low = the pack is at or under the threshold and not charging.
    function batteryLow(level, status) {
        return level <= page.batteryLowLevel && status !== 2;
    }

    // The inline note beside the path segments for the non-happy FSM states.
    // The phase drives the needs-replug / restore-stuck lines (warning); a
    // directFailure token drives the failure-reason line (error). Empty for the
    // steady routed / direct / claiming states.
    function pathNoteText(phase, failure) {
        if (phase === "needsReplug") {
            return qsTr("Unplug and replug the controller to finish switching.");
        }
        if (phase === "restoreStuck") {
            return qsTr("Standard mode isn’t responding — switch to Direct, retry, or replug.");
        }
        if (failure === "permissionDenied") {
            return qsTr("Direct access denied — another app owns this device.");
        }
        if (failure === "busy") {
            return qsTr("Direct claim is busy — another app or driver holds the device.");
        }
        if (failure === "initFailed") {
            return qsTr("Direct claim couldn’t start the controller’s report stream.");
        }
        if (failure === "dropped") {
            return qsTr("The device dropped during the claim — a replug is needed.");
        }
        return "";
    }
    // The phase notes are warning-toned; only a claim-failure reason (with no
    // phase note taking precedence) reads in the error tone.
    function pathNoteIsError(phase, failure) {
        return phase !== "needsReplug" && phase !== "restoreStuck"
               && failure.length > 0;
    }
}
