// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The themed single-line text field (e.g. the 6-digit PIN entry in the pairing
// dialog). A flat, outlined input that highlights its border in `primary` while
// focused. All TextField API (placeholderText, maximumLength, validator,
// inputMethodHints, text) passes straight through.
//
// `hasError` + `errorText` are the field's own failure state: the one input in
// the app is the PIN, and the PIN can be rejected. Field-level validation is
// drawn HERE, never in the toast — a rejection that leaves the sheet open must
// say so on the sheet. The message reserves its own height so a layout never
// jumps when the error appears.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

TextField {
    id: field

    property bool hasError: false
    property string errorText: ""

    // The height the message row costs; 0 when there is nothing to say.
    readonly property int errorExtra: (field.hasError && field.errorText.length > 0)
                                      ? errorLabel.implicitHeight + Tokens.s1 : 0

    // TextField is a TextInput, not a Control, so it has no `visualFocus`.
    // Derive the same rule the rest of the kit rings on: focus that ARRIVED
    // from the keyboard, so a click into the field does not draw the ring.
    readonly property bool keyboardFocus: field.activeFocus
                                          && (field.focusReason === Qt.TabFocusReason
                                              || field.focusReason === Qt.BacktabFocusReason
                                              || field.focusReason === Qt.ShortcutFocusReason)

    implicitHeight: 32 + field.errorExtra
    topPadding: Tokens.s3
    bottomPadding: Tokens.s3 + field.errorExtra
    leftPadding: Tokens.s4
    rightPadding: Tokens.s4
    color: Theme.onSurface
    placeholderTextColor: Theme.muted
    selectionColor: Theme.primary
    selectedTextColor: Theme.onPrimary
    font.pixelSize: Tokens.textBase

    Accessible.description: field.hasError ? field.errorText : ""

    background: Item {
        Rectangle {
            id: frame
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - field.errorExtra
            radius: Tokens.radiusButton
            color: Theme.surface
            border.width: 1
            border.color: field.hasError ? Theme.error
                        : field.activeFocus ? Theme.primary
                        : Theme.outline

            Behavior on border.color {
                enabled: !Tokens.reducedMotion
                ColorAnimation { duration: Tokens.durFast }
            }
        }

        // The global focus ring, outside the frame, on keyboard focus only.
        Rectangle {
            anchors.fill: frame
            anchors.margins: -2
            radius: Tokens.radiusButton + 2
            visible: field.keyboardFocus
            color: "transparent"
            border.width: 2
            border.color: Theme.focusRing
        }

        Text {
            id: errorLabel
            anchors.top: frame.bottom
            anchors.topMargin: Tokens.s1
            anchors.left: frame.left
            anchors.right: frame.right
            visible: field.hasError && field.errorText.length > 0
            text: field.errorText
            color: Theme.error
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
        }
    }
}
