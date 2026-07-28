// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Navigation row card (design FRowBtn): title + sub-line + trailing chevron on
// a surface card. The Settings page's "Setup guide" / "Licenses" / "Support
// Dish" rows. Whole row is the click target.

import QtQuick
import Dish.Chrome

Rectangle {
    id: control

    property string title: ""
    property string subtitle: ""

    signal clicked()

    implicitHeight: column.implicitHeight + 20
    radius: Tokens.radiusCard
    color: mouse.containsMouse ? Qt.tint(Theme.surface, Theme.primaryHover) : Theme.surface
    border.width: 1
    border.color: Theme.outline

    Column {
        id: column
        anchors.left: parent.left
        anchors.right: chevron.left
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: Tokens.s6
        anchors.rightMargin: Tokens.s5
        spacing: 2

        Text {
            width: parent.width
            text: control.title
            font.pixelSize: Tokens.textBase
            font.weight: Font.DemiBold
            color: Theme.onSurface
            elide: Text.ElideRight
        }
        Text {
            width: parent.width
            text: control.subtitle
            font.pixelSize: Tokens.textMeta
            color: Theme.muted
            wrapMode: Text.WordWrap
            visible: text.length > 0
        }
    }

    Text {
        id: chevron
        anchors.right: parent.right
        anchors.rightMargin: Tokens.s6
        anchors.verticalCenter: parent.verticalCenter
        text: "›"
        font.pixelSize: 15
        color: Theme.muted
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
