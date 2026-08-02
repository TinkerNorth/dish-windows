// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The inline advice block — the tone-tinted rounded panel the wizard leans on
// ("A wired pad is the safest first run…", "Bluetooth is off on this PC.").
//
// It is NOT ErrorBanner: a banner reports that something failed and offers a
// retry; a callout is advice attached to the step the user is on. Three tones,
// no fourth: Info is guidance, Warning is a condition the user may want to fix,
// Error is a condition that blocks.
//
// Trailing buttons are the DEFAULT property, so a caller writes the action as a
// child and never has to know the internal row:
//
//   Kit.Callout {
//       tone: Kit.Callout.Warning
//       glyph: "bluetooth-off"
//       text: qsTr("Bluetooth is off on this PC.")
//       Kit.DishButton { text: qsTr("Open Bluetooth settings ↗"); size: Kit.DishButton.Small }
//   }
//
// Because the default property is redirected, everything this file declares
// goes through an explicit property (`background` / `contentItem`) — a bare
// child object here would be re-parented into the action row, inside itself.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Control {
    id: callout

    enum Tone { Info, Warning, Error }

    property int tone: Callout.Info
    property string text: ""
    // Optional leading brand asset name; empty draws no glyph cell at all.
    property string glyph: ""

    // Trailing actions.
    default property alias actions: actionRow.data

    // One colour drives both the wash and the copy — the fill is the tone's
    // wash, the text is the tone at full strength.
    readonly property color toneColor: callout.tone === Callout.Warning ? Theme.warning
                                     : callout.tone === Callout.Error ? Theme.error
                                     : Theme.primary

    topPadding: Tokens.s5
    bottomPadding: Tokens.s5
    leftPadding: Tokens.s6
    rightPadding: Tokens.s6

    Accessible.role: Accessible.StaticText
    Accessible.name: callout.text

    background: Rectangle {
        radius: Tokens.radiusButton
        color: callout.tone === Callout.Warning ? Theme.warningFill
             : callout.tone === Callout.Error ? Theme.errorFill
             : Theme.primaryFill
    }

    contentItem: RowLayout {
        spacing: Tokens.s5

        BrandGlyph {
            glyph: callout.glyph
            visible: callout.glyph.length > 0
            Layout.preferredWidth: Tokens.glyphSm
            Layout.preferredHeight: Tokens.glyphSm
            Layout.alignment: Qt.AlignTop
        }

        Text {
            text: callout.text
            color: callout.toneColor
            font.pixelSize: Tokens.textMeta
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        // Collapses to nothing when the caller passed no action.
        Row {
            id: actionRow
            spacing: Tokens.s4
            visible: actionRow.implicitWidth > 0
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
