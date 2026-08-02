// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// THE button. One type, three variants (Primary / Outline / Destructive) and
// two sizes (Normal / Small). States — hover, press, keyboard focus, disabled —
// are NEVER variants: writing `variant: Off` would lose the accessibility state
// the whole design leans on.
//
// Disabled is `enabled: false` (so UI Automation / Narrator report it) PLUS
// Tokens.disabledOpacity PLUS Theme.disabledFg — never a muted colour
// multiplied by an opacity, which composites to ~2:1 and makes the one control
// the user is waiting for the least readable thing on the page.
//
// KitButton.qml and OutlineButton.qml are one-line aliases over this so the
// existing call sites keep working; new code instantiates DishButton directly.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Button {
    id: control

    enum Variant { Primary, Outline, Destructive }
    enum Size { Normal, Small }

    property int variant: DishButton.Outline
    property int size: DishButton.Normal

    // Card-action density: 11px label in a 24px pill (the design's "small"),
    // not a new font size.
    readonly property bool compact: control.size === DishButton.Small

    // The label colour — and, for the two outlined variants, the border colour.
    readonly property color foreground: !control.enabled ? Theme.disabledFg
                                      : control.variant === DishButton.Primary ? Theme.onPrimary
                                      : control.variant === DishButton.Destructive ? Theme.error
                                      : Theme.primary

    focusPolicy: Qt.StrongFocus
    hoverEnabled: true

    font.pixelSize: control.compact ? Tokens.textMeta : Tokens.textBase
    font.weight: Font.Medium
    implicitHeight: control.compact ? 24 : 30
    leftPadding: control.compact ? Tokens.s5 : Tokens.s6
    rightPadding: control.compact ? Tokens.s5 : Tokens.s6

    opacity: control.enabled ? 1.0 : Tokens.disabledOpacity

    Accessible.role: Accessible.Button
    Accessible.name: control.text

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.foreground
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Item {
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusButton
            // Primary carries its state in the fill; the outlined pair carry it
            // in the wash over a transparent ground so a Mica surface still
            // reads through a button placed on bare chrome.
            color: control.variant === DishButton.Primary
                     ? (!control.enabled ? Theme.surfaceDim
                        : control.down || control.hovered ? Theme.primaryDark
                        : Theme.primary)
                 : control.variant === DishButton.Destructive
                     ? (control.down ? Theme.alpha(Theme.error, 0.18)
                        : control.hovered ? Theme.alpha(Theme.error, 0.12)
                        : "transparent")
                     : (control.down ? Theme.primaryPress
                        : control.hovered ? Theme.primaryHover
                        : "transparent")
            // A filled primary has no border at rest; keyboard focus adds the
            // 1px solid rule the ring sits outside of.
            border.width: control.variant === DishButton.Primary
                          ? (control.visualFocus ? 1 : 0) : 1
            border.color: control.variant === DishButton.Primary
                          ? Theme.primary : control.foreground

            Behavior on color {
                enabled: !Tokens.reducedMotion
                ColorAnimation { duration: Tokens.durFast }
            }
        }

        // The global focus ring: 2px Theme.focusRing OUTSIDE the border, on
        // visualFocus only, so a mouse press never rings.
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: Tokens.radiusButton + 2
            visible: control.visualFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
        }
    }
}
