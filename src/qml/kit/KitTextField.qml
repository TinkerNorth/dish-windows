// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The themed single-line text field (e.g. the 6-digit PIN entry in the pairing
// dialog). A flat, outlined input that highlights its border in `primary` while
// focused. All TextField API (placeholderText, maximumLength, validator,
// inputMethodHints, text) passes straight through.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

TextField {
    id: field

    implicitHeight: 32
    leftPadding: Tokens.s4
    rightPadding: Tokens.s4
    color: Theme.onSurface
    placeholderTextColor: Theme.muted
    selectionColor: Theme.primary
    selectedTextColor: Theme.onPrimary
    font.pixelSize: Tokens.textBase

    background: Rectangle {
        radius: Tokens.radiusButton
        color: Theme.surface
        border.width: 1
        border.color: field.activeFocus ? Theme.primary : Theme.outline
    }
}
