// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The bind chooser (design FBindDlg), shared by the Controllers page's slot
// cards and the Home signal path's "Bind…" ghost card. One instance per page,
// retargeted per open: openFor(slotId, slotName) pulls the slot's filtered
// pick-list ONE-SHOT from App.availableConnectionsForSlot (the same
// PickerVisibility reducer the Widgets SlotCard used — connections bound to
// another slot are excluded, the slot's own binding is held over even offline)
// and accept binds the captured slot to the chosen row. No other state leaks
// out; all data and actions come from the frozen App contract.

// Bound so the delegate references outer ids (chooser, bindList) statically.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.ContentDialog {
    id: chooser

    // The slot the open chooser targets (captured by openFor).
    property string slotId: ""
    property string slotName: ""
    // The connectionId chosen in the list, captured in delegate scope.
    property string chosenConnectionId: ""
    // The rows on offer — {connectionId,label,dotColor,glyph} objects, pulled
    // one-shot on open (the invokable has no NOTIFY).
    property var candidates: []

    eyebrow: qsTr("Bind")
    heading: qsTr("Bind %1").arg(chooser.slotName)
    preferredWidth: 440
    acceptText: qsTr("Bind")
    // A selection is required before the bind can apply.
    acceptEnabled: bindList.currentIndex >= 0

    function openFor(slotId, slotName) {
        chooser.slotId = slotId;
        chooser.slotName = slotName;
        chooser.chosenConnectionId = "";
        chooser.candidates = App.availableConnectionsForSlot(slotId);
        bindList.currentIndex = -1;
        chooser.open();
    }

    body: [
        Label {
            text: qsTr("Choose which satellite this controller drives.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        ListView {
            id: bindList
            Layout.fillWidth: true
            implicitHeight: Math.min(contentHeight, 240)
            clip: true
            spacing: Tokens.s1
            currentIndex: -1
            model: chooser.candidates

            delegate: ItemDelegate {
                id: connRow
                required property int index
                // A pick-list row object (contract §1,
                // availableConnectionsForSlot).
                required property var modelData

                width: ListView.view ? ListView.view.width : implicitWidth
                topPadding: Tokens.s4
                bottomPadding: Tokens.s4
                leftPadding: Tokens.s6
                rightPadding: Tokens.s6
                highlighted: ListView.isCurrentItem
                onClicked: {
                    bindList.currentIndex = connRow.index;
                    // Capture the id here, in delegate scope — the dialog's
                    // accept handler reads it back.
                    chooser.chosenConnectionId = connRow.modelData.connectionId;
                }

                contentItem: RowLayout {
                    spacing: Tokens.s5

                    Kit.RadioMark {
                        selected: connRow.highlighted
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Kit.BrandGlyph {
                        Layout.preferredWidth: 18
                        Layout.preferredHeight: 18
                        glyph: glyphForToken(connRow.modelData.glyph)
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Kit.StatusDot {
                        token: connRow.modelData.dotColor
                        Layout.alignment: Qt.AlignVCenter
                    }
                    Label {
                        text: connRow.modelData.label
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textBase
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    // The design's trailing "Online" cue, derived from the
                    // row's dot token (the pick-list payload carries no chip
                    // text). Only the success state names itself.
                    Label {
                        visible: connRow.modelData.dotColor === "success"
                        text: qsTr("Online")
                        color: Theme.success
                        font.pixelSize: Tokens.textMeta
                        Layout.alignment: Qt.AlignVCenter
                    }
                }

                background: Rectangle {
                    radius: Tokens.radiusButton
                    color: connRow.highlighted ? Theme.primaryFill
                         : connRow.hovered
                               ? Qt.rgba(Theme.onSurface.r, Theme.onSurface.g,
                                         Theme.onSurface.b, 0.06)
                         : "transparent"
                }
            }
        },
        Label {
            text: qsTr("Satellites already driven by another slot are not offered.")
            color: Theme.muted
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    ]

    onAccepted: {
        if (bindList.currentIndex < 0) { return; }
        App.bindSlot(chooser.slotId, chooser.chosenConnectionId);
        close();
    }
}
