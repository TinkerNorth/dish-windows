// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The modal-overlay base for tasks that interrupt the shell (pairing, emulate
// picker). A centered, dimmed-scrim Popup that paints a Card-like surface and
// hosts a title, a content slot, and a footer button row. Parent it to the
// shell's overlay layer so it floats above the nav + content (see QML_UI_KIT.md
// "Overlay convention"); since a Popup reparents to `Overlay.overlay` by default
// when opened, declaring one anywhere in the shell tree and calling `open()`
// just works.
//
// Usage:
//   Kit.ContentDialog {
//       id: pairDialog
//       heading: qsTr("Pair with Living-Room")
//       body: [ /* fields */ ]
//       // footer buttons are provided as `acceptText` / `rejectText`;
//       // accepted()/rejected() fire on click.
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

    // The design scrim: deep-space ink at 55%, not neutral black.
    Overlay.modal: Rectangle {
        color: Qt.rgba(3 / 255, 5 / 255, 16 / 255, 0.55)
    }

    background: Rectangle {
        radius: Tokens.radiusCard
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

            OutlineButton {
                text: dialog.rejectText
                visible: dialog.rejectText.length > 0
                onClicked: { dialog.rejected(); dialog.close(); }
            }
            KitButton {
                text: dialog.acceptText
                visible: dialog.acceptText.length > 0
                enabled: dialog.acceptEnabled
                onClicked: dialog.accepted()   // page decides whether to close
            }
        }
    }
}
