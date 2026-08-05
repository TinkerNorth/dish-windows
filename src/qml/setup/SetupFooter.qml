// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The wizard footer, cloned from SetupWizardPage's pinned row: Back and Cancel
// on the left, the position/hint line filling the middle, the primary on the
// right. Back is DISABLED, never absent — a control that vanishes moves every
// other control under the pointer. The host owns every enablement rule and
// label; this file owns only the geometry.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit

Column {
    id: footer

    property bool backEnabled: false
    property bool cancelEnabled: true
    property string cancelText: qsTr("Cancel")
    property string hintText: ""
    property string primaryLabel: ""
    property bool primaryEnabled: true
    // The uninstaller's Remove is the one destructive commit in the family.
    property bool primaryDestructive: false

    // Focus targets for the per-page focus rule (Welcome lands on the primary,
    // Installing on Cancel).
    readonly property alias primaryButton: primaryBtn
    readonly property alias cancelButton: cancelBtn

    signal backClicked()
    signal cancelClicked()
    signal primaryClicked()

    spacing: Tokens.s6
    bottomPadding: Tokens.s2

    Rectangle {
        width: parent.width
        height: 1
        color: Theme.outline
    }

    RowLayout {
        width: parent.width
        spacing: Tokens.s4

        Kit.DishButton {
            text: qsTr("‹ Back")
            variant: Kit.DishButton.Outline
            enabled: footer.backEnabled
            onClicked: footer.backClicked()
        }
        Kit.DishButton {
            id: cancelBtn
            text: footer.cancelText
            variant: Kit.DishButton.Outline
            enabled: footer.cancelEnabled
            onClicked: footer.cancelClicked()
        }
        Label {
            text: footer.hintText
            color: Theme.muted
            font.pixelSize: Tokens.textMeta
            elide: Text.ElideRight
            Layout.fillWidth: true
            Layout.leftMargin: Tokens.s2
        }
        Kit.DishButton {
            id: primaryBtn
            text: footer.primaryLabel
            variant: footer.primaryDestructive ? Kit.DishButton.Destructive
                                               : Kit.DishButton.Primary
            enabled: footer.primaryEnabled
            onClicked: footer.primaryClicked()
        }
    }
}
