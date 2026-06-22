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
//       contentColumn.children: [ /* fields */ ]
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

    property string heading: ""
    property string acceptText: qsTr("OK")
    property string rejectText: qsTr("Cancel")
    property bool acceptEnabled: true
    // Pages inject their body controls into this column.
    property alias contentColumn: bodyColumn

    signal accepted()
    signal rejected()

    modal: true
    dim: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, (parent ? parent.width : 420) - 48)
    padding: 0
    closePolicy: Popup.CloseOnEscape

    // Dim scrim behind the dialog; the surface itself is the Card below.
    Overlay.modal: Rectangle {
        color: Qt.rgba(0, 0, 0, 0.5)
    }

    background: Rectangle {
        radius: 12
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
    }

    contentItem: ColumnLayout {
        spacing: 16

        Label {
            text: dialog.heading
            visible: dialog.heading.length > 0
            color: Theme.onSurface
            font.pixelSize: 16
            font.bold: true
            Layout.fillWidth: true
            Layout.topMargin: 20
            Layout.leftMargin: 20
            Layout.rightMargin: 20
        }

        ColumnLayout {
            id: bodyColumn
            spacing: 12
            Layout.fillWidth: true
            Layout.leftMargin: 20
            Layout.rightMargin: 20
        }

        RowLayout {
            spacing: 8
            Layout.alignment: Qt.AlignRight
            Layout.bottomMargin: 20
            Layout.leftMargin: 20
            Layout.rightMargin: 20

            OutlineButton {
                text: dialog.rejectText
                onClicked: { dialog.rejected(); dialog.close(); }
            }
            KitButton {
                text: dialog.acceptText
                enabled: dialog.acceptEnabled
                onClicked: dialog.accepted()   // page decides whether to close
            }
        }
    }
}
