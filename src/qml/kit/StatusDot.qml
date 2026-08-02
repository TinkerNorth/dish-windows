// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The status dot. Maps the contract's dot token strings
// ("success"/"warning"/"muted"/"primary"/"error") onto Theme colors so
// delegates never hand-roll the colour ladder. Bind `token` straight from a
// model role's `dotColor` (SlotListModel / ConnectionListModel) — and never
// draw a dot without the chip beside it: hue alone is not a status.
//
// 10px with a 1px darker ring, not 8px flat: at 8px on a Mica backdrop the
// success and warning hues are a coin-flip for a low-vision user, and the ring
// gives the dot an edge independent of its fill.

import QtQuick
import Dish.Chrome

Rectangle {
    id: dot

    // One of "success" / "warning" / "muted" / "primary" / "error" (the
    // contract's dot tokens). Anything unrecognised falls back to muted.
    property string token: "muted"

    readonly property color tone: dot.token === "success" ? Theme.success
                                : dot.token === "warning" ? Theme.warning
                                : dot.token === "primary" ? Theme.primary
                                : dot.token === "error" ? Theme.error
                                : Theme.muted

    implicitWidth: 10
    implicitHeight: 10
    radius: width / 2

    // The dot never speaks: it always travels with a chip, and the chip carries
    // the word. Announcing both would read every status twice.
    Accessible.ignored: true

    color: dot.tone
    border.width: 1
    border.color: Qt.darker(dot.tone, 1.4)
}
