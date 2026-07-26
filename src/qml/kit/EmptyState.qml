// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A centered empty placeholder for the "no items yet" state — the shared
// replacement for the hand-rolled empty Cards in ControllersPage /
// ConnectionsPage / LicensesPage. A bold `title` in Theme.onSurface over a muted
// `body` line, with an optional call-to-action button (shown when `showAction`)
// that emits `actionRequested()`. Muted styling throughout; drop it inside a
// Kit.Card (so it reads against a surface, not bare Mica) or a Kit.Page.
//
// Usage:
//   Kit.Card {
//       Kit.EmptyState {
//           title: qsTr("No controllers yet")
//           body: qsTr("Plug in a controller or connect a satellite to add a slot.")
//           actionText: qsTr("Scan")
//           showAction: true
//           onActionRequested: App.startDiscovery()
//       }
//   }

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

ColumnLayout {
    id: empty

    // The headline ("No controllers yet").
    property string title: ""
    // The muted sub-text under the title.
    property string body: ""
    // The optional call-to-action button label.
    property string actionText: ""
    // Whether to show the call-to-action button.
    property bool showAction: false
    // Optional brand asset name drawn 40px above the title (the design's
    // dish-off glyph on the empty dashboard). Empty = text only.
    property string glyph: ""

    // Emitted when the user taps the action — the page decides what it does.
    signal actionRequested()

    spacing: Tokens.s5

    BrandGlyph {
        glyph: empty.glyph
        width: 40
        height: 40
        visible: empty.glyph.length > 0
        Layout.alignment: Qt.AlignHCenter
    }

    Label {
        text: empty.title
        visible: empty.title.length > 0
        color: Theme.onSurface
        font.pixelSize: 14
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        Layout.alignment: Qt.AlignHCenter
    }

    Label {
        text: empty.body
        visible: empty.body.length > 0
        color: Theme.muted
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
        // The design caps empty-state copy at a readable measure.
        Layout.maximumWidth: 400
        Layout.alignment: Qt.AlignHCenter
    }

    // Outlined call-to-action, centered under the copy (the design's quiet
    // "Open Connections"). Emits the signal rather than acting, so the page
    // owns the behavior.
    OutlineButton {
        text: empty.actionText
        visible: empty.showAction && empty.actionText.length > 0
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 6
        onClicked: empty.actionRequested()
    }
}
