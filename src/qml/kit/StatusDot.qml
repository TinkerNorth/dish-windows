// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The status dot. Maps the contract's dot token strings
// ("success"/"warning"/"muted"/"primary") onto Theme colors so delegates never
// hand-roll the colour ladder. Bind `token` straight from a model role's
// `dotColor` (SlotListModel / ConnectionListModel).

import QtQuick
import Dish.Chrome

Rectangle {
    // One of "success" / "warning" / "muted" / "primary" (the contract's dot
    // tokens). Anything unrecognised falls back to muted.
    property string token: "muted"

    implicitWidth: 8
    implicitHeight: 8
    radius: width / 2

    color: token === "success" ? Theme.success
         : token === "warning" ? Theme.warning
         : token === "primary" ? Theme.primary
         : Theme.muted
}
