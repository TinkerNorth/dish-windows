// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The dashed ACTION CARD — the design's list-density invitation control
// ("+ Add a controller", the unbound pad's "Bind…"), frame 18 "Action card —
// states". A dashed accent border over the primary-fill wash; the cyan wash
// deepens on interaction (rest = Theme.primaryFill, hover 18 %, pressed 24 %),
// keyboard focus turns the border solid and adds a 2px accent ring, and
// disabled drops the whole control to 0.4 opacity — the canonical Dish cue.
// Title reads in the accent, the sub-line muted. The pane-density sibling (the
// rail's compact "+ Add") lives in AppShell — same vocabulary, solid outline.
//
// The dashed stroke is a Canvas (Qt 6.7 Shapes has no rounded dashed rect
// primitive); it repaints on palette / focus / enabled flips like the title
// bar's glyph canvases.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

AbstractButton {
    id: card

    // The accent headline ("+ Add a controller" — pass showPlus for the +).
    property string title: ""
    // The muted explainer line ("Wired, or Bluetooth").
    property string subtitle: ""
    // Draw the leading "+" glyph (the Add variant; Bind… has none).
    property bool showPlus: false

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    topPadding: 10
    bottomPadding: 10
    leftPadding: Tokens.s6
    rightPadding: Tokens.s6

    opacity: card.enabled ? 1.0 : Tokens.disabledOpacity

    HoverHandler { cursorShape: Qt.PointingHandCursor }

    background: Item {
        // Keyboard-focus ring: 2px accent at 30 %, just outside the border
        // (design boxShadow 0 0 0 2px). visualFocus keeps a mouse press from
        // ringing — the ring is the keyboard cue.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Tokens.radiusCard + 2
            visible: card.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.3)
        }

        // The wash: rest = the primary-fill token; hover / pressed deepen to
        // the design's 18 / 24 % accent alphas.
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusCard
            color: card.down ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.24)
                 : card.hovered ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                 : Theme.primaryFill
        }

        // The dashed accent border; solid under keyboard focus.
        Canvas {
            id: borderCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = String(Theme.primary);
                ctx.lineWidth = 1;
                if (!card.visualFocus)
                    ctx.setLineDash([3, 3]);
                ctx.beginPath();
                ctx.roundedRect(0.5, 0.5, width - 1, height - 1,
                                Tokens.radiusCard, Tokens.radiusCard);
                ctx.stroke();
            }
            Connections {
                target: Theme
                function onPaletteChanged() { borderCanvas.requestPaint(); }
            }
            Connections {
                target: card
                function onVisualFocusChanged() { borderCanvas.requestPaint(); }
            }
        }
    }

    contentItem: Row {
        spacing: Tokens.s5

        Text {
            visible: card.showPlus
            text: "+"
            color: Theme.primary
            font.pixelSize: 16
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            spacing: Tokens.s1
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: card.title
                color: Theme.primary
                font.pixelSize: Tokens.textBase
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth,
                                card.availableWidth - (card.showPlus ? 16 + Tokens.s5 : 0))
            }
            Text {
                visible: card.subtitle.length > 0
                text: card.subtitle
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
                elide: Text.ElideRight
                width: Math.min(implicitWidth,
                                card.availableWidth - (card.showPlus ? 16 + Tokens.s5 : 0))
            }
        }
    }
}
