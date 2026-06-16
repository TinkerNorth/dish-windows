// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Controllers destination — the slot/controller dashboard. Reproduces the
// Widgets SlotCard (name, status dot, bound state, the capability + live-stat
// chips) for every row of App.slotModel, with per-row Bind/Unbind, an Emulate
// picker for bound slots, and an empty-state when there are no slots. All data
// and actions come solely from the frozen App contract (docs/QML_CONTRACT.md);
// no business logic lives here.

// Bound so the inline Chip component and delegates may reference outer ids
// (page) without qmllint flagging them; also makes delegate model bindings
// resolvable via `required property`.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Controllers")

    // The slot whose bind chooser is open. Captured on row click so the shared
    // chooser dialog (declared once, not per-delegate) knows which slot to bind.
    property string bindSlotId: ""
    // The connectionId chosen in the bind chooser, captured in the delegate
    // scope (where the role is visible) so accept needn't reach into model.data.
    property string bindConnectionId: ""
    // The slot whose emulate picker is open, mirrored for the same reason.
    property string emulateSlotId: ""

    Kit.SectionHeader { label: qsTr("Controllers") }

    // Empty-state: shown only when the model is genuinely empty. A Card (not bare
    // Mica) so the message reads against a surface like the rest of the page.
    Kit.Card {
        visible: App.slotModel.count === 0 // qmllint disable unqualified
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 4
            Label {
                text: qsTr("No controllers yet")
                color: Theme.onSurface
                font.pixelSize: 14
                font.bold: true
            }
            Label {
                text: qsTr("Plug in a controller or connect a satellite to add a slot.")
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // The slot list. Each row is a Card reproducing the Widgets SlotCard. The
    // ListView is sized to its contents (the page's Column lays children out
    // vertically) so it grows with the model rather than scrolling internally.
    ListView {
        id: slotList
        visible: App.slotModel.count > 0 // qmllint disable unqualified
        width: parent ? parent.width : implicitWidth
        height: contentHeight
        interactive: false
        spacing: 12
        model: App.slotModel // qmllint disable unqualified

        delegate: Kit.Card {
            id: card
            width: ListView.view ? ListView.view.width : implicitWidth

            // Slot roles consumed by this delegate (contract §2). Declared
            // required so they resolve qualified and the ListView injects them.
            required property string slotId
            required property string name
            required property bool bound
            required property string boundLabel
            required property bool live
            required property string dotColor
            required property bool usbDirect
            required property bool hasMotion
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

            // Brand glyph variant follows the live state, like the Widgets card's
            // leading satellite silhouette.
            readonly property string glyphAsset: card.live ? "satellite-connected"
                                                 : card.bound ? "satellite"
                                                 : "satellite-off"

            contentItem: RowLayout {
                spacing: 12

                Kit.BrandGlyph {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    glyph: card.glyphAsset
                    Layout.alignment: Qt.AlignVCenter
                }

                Kit.StatusDot {
                    token: card.dotColor
                    Layout.alignment: Qt.AlignVCenter
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2

                    Label {
                        text: card.name
                        color: Theme.onSurface
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Label {
                        text: card.bound ? qsTr("Bound to %1").arg(card.boundLabel)
                                         : qsTr("Unbound")
                        color: Theme.muted
                        font.pixelSize: 11
                    }

                    // Capability + live-stat chip row. The capability pills sit at
                    // the leading edge; the measured-Hz cluster right-aligns after
                    // a stretch, mirroring the Widgets chipRow layout.
                    RowLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 4
                        spacing: 6

                        // Motion capability: explicit Gyro / No-gyro so absence is
                        // visible, not merely the lack of an indicator.
                        Chip {
                            text: card.hasMotion ? qsTr("Gyro") : qsTr("No gyro")
                            tone: card.hasMotion ? "present" : "absent"
                        }
                        // Lightbar shown ONLY when the pad has an RGB LED.
                        Chip {
                            visible: card.hasLightbar
                            text: qsTr("Lightbar")
                            tone: "present"
                        }
                        // Battery chip only when a real reading has landed.
                        Chip {
                            visible: card.batteryKnown
                            text: page.batteryLabel(card.batteryLevel, card.batteryStatus)
                            tone: page.batteryLow(card.batteryLevel, card.batteryStatus)
                                  ? "warning" : "present"
                        }

                        Item { Layout.fillWidth: true }

                        // Measured-rate cluster. Each honors its *Shown role and
                        // formats live vs ~peak from the *Live role.
                        Chip {
                            visible: card.gamepadHzShown
                            text: card.gamepadHzLive ? qsTr("%1 Hz").arg(card.gamepadHz)
                                                     : qsTr("~%1 Hz").arg(card.gamepadHz)
                            tone: card.gamepadHzLive ? "live" : "muted"
                        }
                        Chip {
                            visible: card.motionHzShown
                            // USB-direct motion is a measured reading; routed is a peak.
                            text: card.usbDirect ? qsTr("%1 Hz").arg(card.motionHz)
                                                 : qsTr("~%1 Hz").arg(card.motionHz)
                            tone: card.usbDirect ? "live" : "muted"
                        }
                        Chip {
                            visible: card.pollHzShown
                            text: qsTr("%1 Hz").arg(card.pollHz)
                            tone: "live"
                        }
                    }
                }

                // Emulate is offered only on a bound slot (nothing to emulate a
                // pad on until it routes to a satellite).
                Kit.OutlineButton {
                    visible: card.bound
                    text: qsTr("Emulate…")
                    Layout.alignment: Qt.AlignVCenter
                    onClicked: page.openEmulate(card.slotId)
                }

                Kit.KitButton {
                    text: card.bound ? qsTr("Unbind") : qsTr("Bind…")
                    Layout.alignment: Qt.AlignVCenter
                    // Disabled when unbound and there is nothing to bind to — the
                    // dimmed control reads as "nothing to do here".
                    enabled: card.bound || App.connectionModel.count > 0 // qmllint disable unqualified
                    onClicked: {
                        if (card.bound) {
                            App.unbindSlot(card.slotId); // qmllint disable unqualified
                        } else {
                            page.openBind(card.slotId);
                        }
                    }
                }
            }
        }
    }

    // ---- Bind chooser (shared; one per page, retargeted per click) ----------
    // Lists the remembered/derived connections from App.connectionModel and
    // binds the captured slot to the chosen one.
    Kit.ContentDialog {
        id: bindDialog
        heading: qsTr("Bind controller")
        acceptText: qsTr("Bind")
        // A selection is required before the bind can apply.
        acceptEnabled: bindList.currentIndex >= 0

        // contentColumn is a frozen Kit.ContentDialog alias (QML_UI_KIT.md §4);
        // the linter cannot see the alias target's children list (known limit).
        contentColumn.children: [
            Label { // qmllint disable missing-property
                text: qsTr("Choose a connection to route this controller to.")
                color: Theme.muted
                font.pixelSize: 12
                Layout.fillWidth: true
            },
            ListView { // qmllint disable missing-property
                id: bindList
                Layout.fillWidth: true
                implicitHeight: Math.min(contentHeight, 240)
                clip: true
                spacing: 4
                currentIndex: -1
                model: App.connectionModel // qmllint disable unqualified

                delegate: ItemDelegate {
                    id: connRow
                    // Connection roles consumed here (contract §3).
                    required property int index
                    required property string connectionId
                    required property string label
                    required property string ip
                    required property int udpPort
                    required property string dotColor
                    required property string glyph

                    width: ListView.view ? ListView.view.width : implicitWidth
                    highlighted: ListView.isCurrentItem
                    onClicked: {
                        bindList.currentIndex = connRow.index;
                        // Capture the id here, in delegate scope, where the role
                        // is visible — the dialog's accept handler reads it back.
                        page.bindConnectionId = connRow.connectionId;
                    }

                    contentItem: RowLayout {
                        spacing: 10
                        Kit.BrandGlyph {
                            Layout.preferredWidth: 18
                            Layout.preferredHeight: 18
                            glyph: glyphForToken(connRow.glyph)
                            Layout.alignment: Qt.AlignVCenter
                        }
                        Kit.StatusDot {
                            token: connRow.dotColor
                            Layout.alignment: Qt.AlignVCenter
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 0
                            Label {
                                text: connRow.label
                                color: Theme.onSurface
                                font.pixelSize: 13
                            }
                            Label {
                                text: qsTr("%1 • UDP %2").arg(connRow.ip).arg(connRow.udpPort)
                                color: Theme.muted
                                font.pixelSize: 11
                            }
                        }
                    }

                    background: Rectangle {
                        radius: 8
                        color: connRow.highlighted
                                   ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                             : connRow.hovered
                                   ? Qt.rgba(Theme.onSurface.r, Theme.onSurface.g, Theme.onSurface.b, 0.06)
                             : "transparent"
                    }
                }
            }
        ]

        onAccepted: {
            if (bindList.currentIndex < 0) { return; }
            App.bindSlot(page.bindSlotId, page.bindConnectionId); // qmllint disable unqualified
            close();
        }
    }

    // ---- Emulate picker ------------------------------------------------------
    EmulatePicker {
        id: emulatePicker
        onChosen: function(type) {
            App.setControllerType(page.emulateSlotId, type); // qmllint disable unqualified
            emulatePicker.close();
        }
    }

    // ---- Helpers (presentation only; no business logic) ---------------------

    function openBind(slotId) {
        page.bindSlotId = slotId;
        page.bindConnectionId = "";
        bindList.currentIndex = -1;
        bindDialog.open();
    }

    function openEmulate(slotId) {
        page.emulateSlotId = slotId;
        // Kick a best-effort catalog refresh before reading the offer list so a
        // freshly-opened picker shows current types (contract requirement).
        App.refreshEmulate(slotId); // qmllint disable unqualified
        emulatePicker.load(App.emulateTypes(slotId), App.emulateCurrentType(slotId));
        emulatePicker.open();
    }

    // Battery wire-status mapping — mirrors SlotCard.cpp's locals (2=charging,
    // 3=full, 4=wired). batteryKnown already gates visibility, so 0xFF never
    // reaches here.
    function batteryLabel(level, status) {
        if (status === 2) { return qsTr("Battery %1% ↑").arg(level); }   // charging
        if (status === 4) { return qsTr("Battery wired"); }
        if (status === 3) { return qsTr("Battery full"); }
        return qsTr("Battery %1%").arg(level);
    }
    function batteryLow(level, status) {
        // A wired/charging pad is never "low"; only an actually-draining pack
        // trips the amber warning style (matches SlotCard's threshold).
        return level < 15 && status !== 2 && status !== 4;
    }

    // ---- Inline chip -------------------------------------------------------
    // The small pill used for capability + live-stat readouts. Tones: "present"
    // (filled primary), "absent" (dimmed outline), "warning" (amber), "live"
    // (success — a continuous measurement), "muted" (estimate / ~peak).
    component Chip: Rectangle {
        id: chip
        property string text: ""
        property string tone: "muted"
        visible: text.length > 0

        readonly property color toneColor: tone === "present" ? Theme.primary
                                          : tone === "warning" ? Theme.warning
                                          : tone === "live" ? Theme.success
                                          : Theme.muted
        readonly property bool filled: tone === "present" || tone === "warning"
                                        || tone === "live"

        implicitWidth: chipText.implicitWidth + 16
        implicitHeight: 20
        radius: 10
        color: filled ? Qt.rgba(toneColor.r, toneColor.g, toneColor.b, 0.16)
                      : "transparent"
        border.width: filled ? 0 : 1
        border.color: Theme.outline

        Text {
            id: chipText
            anchors.centerIn: parent
            text: chip.text
            color: chip.filled ? chip.toneColor : Theme.muted
            font.pixelSize: 11
        }
    }
}
