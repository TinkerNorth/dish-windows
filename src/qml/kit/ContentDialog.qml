// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The modal-overlay base for tasks that interrupt the shell (pairing, apply,
// blockers, confirms). A centered Popup over Theme.scrim that paints a
// Card-like surface and hosts an eyebrow, a heading, a content slot and a
// footer button row. Parent it to the shell's overlay layer so it floats above
// the nav + content (see QML_UI_KIT.md "Overlay convention"); since a Popup
// reparents to `Overlay.overlay` by default when opened, declaring one anywhere
// in the shell tree and calling `open()` just works.
//
// NO SHADOW. Dish is flat: the scrim already communicates modality, and the
// toast is the one elevated surface in the app (it is the only one that floats
// without a scrim). A blurred shadow per dialog is an extra composited layer on
// a frameless window that is already compositing a Mica backdrop, for zero
// information gain.
//
// Accept does NOT auto-close — the page decides, because an accept that fails
// (a rejected PIN) must keep its surface open.
//
// Usage:
//   Kit.ContentDialog {
//       id: pairDialog
//       heading: qsTr("Pair with Living-Room")
//       body: [ /* fields */ ]
//   }
//   ... pairDialog.open()

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Popup {
    id: dialog

    // The mono accent micro-label above the heading ("PAIRING", "BIND", ...).
    property string eyebrow: ""
    property string heading: ""
    // An empty accept/reject text hides that button (the reverse-pairing sheet
    // offers only Cancel; a pure-info sheet only Next).
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool acceptEnabled: true
    // The accept is the destructive one (Forget host, Discard setup): red
    // outline instead of the accent fill, so the safe choice keeps the weight.
    property bool destructiveAccept: false
    // The design's dialog widths run 400-470 by task; default to the FDlg base.
    property int preferredWidth: 430
    // Pages inject their body controls here. Aliased straight to the body
    // column's data list so the property carries a concrete list type — an
    // object alias would hide the target's members from qmllint and force
    // every call site to suppress missing-property.
    property alias body: bodyColumn.data

    signal accepted()
    signal rejected()

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(preferredWidth, (parent ? parent.width : preferredWidth) - 48)
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // One scrim for every dialog, derived from the palette so a theme flip
    // re-tints it too.
    Overlay.modal: Rectangle {
        color: Theme.scrim
    }

    background: Rectangle {
        radius: Tokens.radiusDialog
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
    }

    contentItem: ColumnLayout {
        spacing: Tokens.s6

        Eyebrow {
            text: dialog.eyebrow
            visible: dialog.eyebrow.length > 0
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s9
            Layout.leftMargin: Tokens.s9
            Layout.rightMargin: Tokens.s9
        }

        Label {
            text: dialog.heading
            visible: dialog.heading.length > 0
            color: Theme.onSurface
            font.pixelSize: Tokens.textHeading
            font.bold: true
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            // The eyebrow, when present, owns the top inset; tuck the heading
            // right under it (the FDlg -4px pull).
            Layout.topMargin: dialog.eyebrow.length > 0 ? -Tokens.s2 : Tokens.s9
            Layout.leftMargin: Tokens.s9
            Layout.rightMargin: Tokens.s9
        }

        ColumnLayout {
            id: bodyColumn
            spacing: Tokens.s6
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.s9
            Layout.rightMargin: Tokens.s9
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.alignment: Qt.AlignRight
            Layout.bottomMargin: Tokens.s9
            Layout.leftMargin: Tokens.s9
            Layout.rightMargin: Tokens.s9

            DishButton {
                text: dialog.rejectText
                visible: dialog.rejectText.length > 0
                variant: DishButton.Outline
                onClicked: { dialog.rejected(); dialog.close(); }
            }
            DishButton {
                text: dialog.acceptText
                visible: dialog.acceptText.length > 0
                enabled: dialog.acceptEnabled
                variant: dialog.destructiveAccept ? DishButton.Destructive
                                                  : DishButton.Primary
                onClicked: dialog.accepted()   // page decides whether to close
            }
        }
    }
}
