// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Radio selection mark (design FRadio): a 15px ring, accent-bordered with a
// 7px accent dot when selected. Pure indicator — the enclosing row owns the
// click surface so the whole row toggles, as the bind/emulate choosers do.

import QtQuick
import Dish.Chrome

Rectangle {
    property bool selected: false

    width: 15
    height: 15
    radius: 7.5
    color: "transparent"
    border.width: 1.5
    border.color: selected ? Theme.primary : Theme.outline

    Rectangle {
        anchors.centerIn: parent
        width: 7
        height: 7
        radius: 3.5
        color: Theme.primary
        visible: parent.selected
    }
}
