// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A surface container: the rounded, outlined panel that groups content (a slot
// card, a connection row, a settings group). Unlike the window chrome this IS an
// opaque-ish surface — it deliberately paints `Theme.surface` so content reads
// against a panel rather than directly on the Mica backdrop. Put a Column/Layout
// inside; `padding` insets it.
//
// Two variants, so no page ever hand-rolls a bordered div again:
//   filled: false  — the hairline box with no fill (Home's wiring row, the
//                    wizard banner slots).
//   dense:  true   — the tight inset (8/10) for node-sized cards. NOT 7/9:
//                    those insets do not exist on the spacing scale.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Control {
    id: card

    // false -> transparent ground, hairline kept.
    property bool filled: true
    // true -> s4/s5 insets instead of the s6/s7 card inset.
    property bool dense: false

    // The ds card inset: 12 vertical / 14 horizontal (slot rows, list panels).
    topPadding: card.dense ? Tokens.s4 : Tokens.s6
    bottomPadding: card.dense ? Tokens.s4 : Tokens.s6
    leftPadding: card.dense ? Tokens.s5 : Tokens.s7
    rightPadding: card.dense ? Tokens.s5 : Tokens.s7

    background: Rectangle {
        radius: Tokens.radiusCard
        color: card.filled ? Theme.surface : "transparent"
        border.width: 1
        border.color: Theme.outline
    }
}
