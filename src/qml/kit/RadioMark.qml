// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Radio selection mark (design FRadio): a 15px ring, accent-bordered with a
// 7px accent dot when selected. Pure indicator — the enclosing row (SelectRow)
// owns the click surface, the focus ring and the keyboard model, so the whole
// row toggles.
//
// It is deliberately invisible to accessibility: the row already announces
// itself as a RadioButton with its checked state, and a second announcement for
// the glyph inside it would make every list read twice.

import QtQuick
import Dish.Chrome

Rectangle {
    id: mark

    property bool selected: false

    width: 15
    height: 15
    radius: width / 2
    color: "transparent"
    border.width: 1.5
    border.color: !mark.enabled ? Theme.disabledFg
                : mark.selected ? Theme.primary
                : Theme.outline

    Accessible.ignored: true

    Rectangle {
        anchors.centerIn: parent
        width: 7
        height: 7
        radius: width / 2
        color: mark.enabled ? Theme.primary : Theme.disabledFg
        visible: mark.selected
    }
}
