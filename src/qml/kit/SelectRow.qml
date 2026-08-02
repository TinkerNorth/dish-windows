// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// THE selectable row. One component draws every "pick exactly one of these"
// list in the app: the wizard's pad picker and host picker, the bind
// destination list, the type list. There is no second radio row.
//
// Selection is 1px Theme.primary + Theme.primaryFill — NEVER a 2px border. The
// only 2px border in the app is the capture-armed card on Configure controls,
// and that is an armed state, not a selection.
//
// The row does not own the group: it reports `picked()` and renders whatever
// `selected` the caller binds, so the truth stays with the page. Up/Down move
// AND select among sibling rows, because a control with one value has no
// meaningful difference between "focused" and "chosen" — the arrow keys are how
// a keyboard user reads the list.
//
// The `extra` slot is the DEFAULT property, so a caller writes trailing chips
// as children:
//
//   Kit.SelectRow {
//       title: padName
//       subtitle: padLine
//       onPicked: draft.adopt(padId)
//       Flow { Kit.CapabilityChip { text: qsTr("Gyro") } }
//   }

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

AbstractButton {
    id: control

    property bool selected: false
    property string title: ""
    property string subtitle: ""
    // Optional leading brand asset name (host rows draw their satellite glyph).
    property string glyph: ""
    // Optional StatusDot token; it rides directly beside the chip so the row
    // never shows a bare dot — hue alone is not a status.
    property string dotToken: ""
    // Optional trailing CapabilityChip.
    property string chipText: ""
    property int chipTone: CapabilityChip.Neutral

    // Right-hand slot: chips, badges, anything the row should carry after its
    // copy. Declared last so a caller's children land here.
    default property alias extra: extraSlot.data

    // The caller applies the choice; the row never writes `selected` itself.
    signal picked()

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    topPadding: Tokens.s3
    bottomPadding: Tokens.s3
    leftPadding: Tokens.s5
    rightPadding: Tokens.s5

    // Every clickable row is at least a comfortable hit target (Tokens.hitRow),
    // however short its copy.
    implicitHeight: Math.max(Tokens.hitRow,
                             control.implicitContentHeight
                             + control.topPadding + control.bottomPadding)

    opacity: control.enabled ? 1.0 : Tokens.disabledOpacity

    Accessible.role: Accessible.RadioButton
    Accessible.checked: control.selected
    Accessible.name: control.title
                     + (control.subtitle.length > 0 ? " · " + control.subtitle : "")
                     + (control.chipText.length > 0 ? " · " + control.chipText : "")

    onClicked: control.picked()

    Keys.onUpPressed: control.stepSelection(false)
    Keys.onDownPressed: control.stepSelection(true)

    // Move the selection to the adjacent sibling row and choose it. Clamped at
    // both ends — a held arrow key must not wrap a destination choice around.
    // The sibling test is structural (same parent, exposes picked()) so this
    // works identically for a ListView delegate and a Column child.
    function stepSelection(forward) {
        if (!control.enabled)
            return;
        // Indexed through a variable, not `item.picked`: the focus chain is
        // typed as a bare Item, so a dotted access would not resolve statically.
        const sig = "picked";
        let item = control;
        for (let guard = 0; guard < 64; ++guard) {
            item = item.nextItemInFocusChain(forward);
            if (!item || item === control)
                return;
            if (item.parent === control.parent && typeof item[sig] === "function") {
                item.forceActiveFocus(Qt.TabFocusReason);
                item[sig]();
                return;
            }
        }
    }

    // The default property is redirected to the `extra` slot, so this file's own
    // objects all go through an explicit property: a bare child here (the cursor
    // handler included) would be re-parented into that slot and only cover it.
    background: Item {
        HoverHandler { cursorShape: Qt.PointingHandCursor }

        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusCard
            color: control.selected
                     ? (control.down ? Theme.accentWash24
                        : control.hovered ? Theme.primaryPress
                        : Theme.primaryFill)
                     : (control.down ? Theme.primaryPress
                        : control.hovered ? Theme.primaryHover
                        : "transparent")
            border.width: 1
            border.color: !control.enabled ? Theme.disabledFg
                        : control.selected ? Theme.primary
                        : Theme.outline

            Behavior on color {
                enabled: !Tokens.reducedMotion
                ColorAnimation { duration: Tokens.durFast }
            }
        }

        // The global focus ring: 2px outside the border, on visualFocus only.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Tokens.radiusCard + 2
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
        }
    }

    contentItem: RowLayout {
        spacing: Tokens.s5

        RadioMark {
            selected: control.selected
            enabled: control.enabled
            Layout.alignment: Qt.AlignVCenter
        }

        BrandGlyph {
            glyph: control.glyph
            visible: control.glyph.length > 0
            Layout.preferredWidth: Tokens.glyphMd
            Layout.preferredHeight: Tokens.glyphMd
            Layout.alignment: Qt.AlignVCenter
        }

        ColumnLayout {
            spacing: Tokens.s1
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            Layout.alignment: Qt.AlignVCenter

            Text {
                text: control.title
                color: !control.enabled ? Theme.disabledFg
                     : control.selected ? Theme.primary : Theme.onSurface
                font.pixelSize: Tokens.textSummary
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            Text {
                text: control.subtitle
                visible: control.subtitle.length > 0
                // A sub-line is information the user reads, so it never rides an
                // opacity — mutedStrong is a colour, tuned for contrast.
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }

        // Caller-provided chips / badges. A positioner child with no explicit
        // width reports its natural extent here, so the copy column (fillWidth
        // + elide) is what gives way first at a narrow window.
        Item {
            id: extraSlot
            implicitWidth: extraSlot.childrenRect.width
            implicitHeight: extraSlot.childrenRect.height
            visible: extraSlot.implicitWidth > 0
            Layout.alignment: Qt.AlignVCenter
        }

        StatusDot {
            token: control.dotToken
            visible: control.dotToken.length > 0
            Layout.alignment: Qt.AlignVCenter
        }

        CapabilityChip {
            text: control.chipText
            tone: control.chipTone
            visible: control.chipText.length > 0
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
