// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The dashed ACTION CARD — the design's list-density invitation control
// ("+ Add a controller", the unbound pad's "Bind…"), frame 18 "Action card —
// states". A dashed accent border over the primary-fill wash; the cyan wash
// deepens on interaction (rest = Theme.primaryFill, hover = Theme.primaryPress
// at 18 %, pressed = Theme.accentWash24), keyboard focus turns the border solid
// and adds the 2px Theme.focusRing ring, and disabled drops the control to
// Tokens.disabledOpacity with Theme.disabledFg text.
//
// `placeholder` is the same dashed shape with no action in it (the wizard
// banner's empty pad / host slot): outline-coloured dash, no wash, no hover, no
// press, no focus ring, no cursor change and NO opacity change — an empty slot
// is information, not a dead control.
//
// The dashed stroke is a Canvas (Qt 6.7 Shapes has no rounded dashed rect
// primitive); it repaints on palette / focus / mode flips like the title bar's
// glyph canvases. Colours reach the 2D context through Qt.rgba(), never
// String(color): QML stringifies a translucent colour as #AARRGGBB and Canvas
// parses it as #RRGGBBAA.

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
    // An ActionCard that is not an action: the dashed style keeps one owner.
    property bool placeholder: false

    focusPolicy: card.placeholder ? Qt.NoFocus : Qt.StrongFocus
    hoverEnabled: !card.placeholder

    topPadding: Tokens.s5
    bottomPadding: Tokens.s5
    leftPadding: Tokens.s6
    rightPadding: Tokens.s6

    // A placeholder is never dimmed — it is drawn-but-unavailable INFORMATION,
    // and the 0.55 rule is legal only on a control the user could otherwise
    // press.
    opacity: (card.placeholder || card.enabled) ? 1.0 : Tokens.disabledOpacity

    Accessible.role: card.placeholder ? Accessible.StaticText : Accessible.Button
    Accessible.name: card.title
    Accessible.description: card.subtitle

    HoverHandler {
        enabled: !card.placeholder
        cursorShape: Qt.PointingHandCursor
    }

    background: Item {
        // Keyboard-focus ring: 2px Theme.focusRing just outside the border
        // (design boxShadow 0 0 0 2px). visualFocus keeps a mouse press from
        // ringing — the ring is the keyboard cue.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Tokens.radiusCard + 2
            visible: card.visualFocus && !card.placeholder
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
        }

        // The wash: rest = the primary-fill token; hover / pressed deepen to
        // the design's 18 / 24 % accent alphas. A placeholder has none.
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusCard
            color: card.placeholder ? "transparent"
                 : card.down ? Theme.accentWash24
                 : card.hovered ? Theme.primaryPress
                 : Theme.primaryFill

            Behavior on color {
                enabled: !Tokens.reducedMotion
                ColorAnimation { duration: Tokens.durFast }
            }
        }

        // The dashed border: accent for an action, the plain hairline for a
        // placeholder; solid under keyboard focus.
        Canvas {
            id: borderCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                var c = card.placeholder ? Theme.outline
                      : card.enabled ? Theme.primary : Theme.disabledFg;
                ctx.strokeStyle = Qt.rgba(c.r, c.g, c.b, c.a);
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
                function onPlaceholderChanged() { borderCanvas.requestPaint(); }
                function onEnabledChanged() { borderCanvas.requestPaint(); }
            }
        }
    }

    contentItem: Row {
        spacing: Tokens.s5

        Text {
            visible: card.showPlus
            text: "+"
            color: card.placeholder ? Theme.mutedStrong
                 : card.enabled ? Theme.primary : Theme.disabledFg
            font.pixelSize: Tokens.textHeading
            anchors.verticalCenter: parent.verticalCenter
        }
        Column {
            spacing: Tokens.s1
            anchors.verticalCenter: parent.verticalCenter

            Text {
                text: card.title
                color: card.placeholder ? Theme.mutedStrong
                     : card.enabled ? Theme.primary : Theme.disabledFg
                font.pixelSize: Tokens.textBase
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                width: Math.min(implicitWidth,
                                card.availableWidth
                                - (card.showPlus ? Tokens.textHeading + Tokens.s5 : 0))
            }
            Text {
                visible: card.subtitle.length > 0
                text: card.subtitle
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
                elide: Text.ElideRight
                width: Math.min(implicitWidth,
                                card.availableWidth
                                - (card.showPlus ? Tokens.textHeading + Tokens.s5 : 0))
            }
        }
    }
}
