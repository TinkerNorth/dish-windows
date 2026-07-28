// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A surface container: the rounded, outlined panel that groups content (a slot
// card, a connection row, a settings group). Unlike the window chrome this IS an
// opaque-ish surface — it deliberately paints `Theme.surface` so content reads
// against a panel rather than directly on the Mica backdrop. Put a Column/Layout
// inside; `padding` insets it.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Control {
    id: card

    // The ds card inset: 12 vertical / 14 horizontal (slot rows, list panels).
    topPadding: Tokens.s6
    bottomPadding: Tokens.s6
    leftPadding: Tokens.s7
    rightPadding: Tokens.s7

    background: Rectangle {
        radius: Tokens.radiusCard
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
    }
}
