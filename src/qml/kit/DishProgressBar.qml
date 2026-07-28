// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Thin progress bar (design-system ProgressBar): with `indeterminate` it runs
// the 1.1s accent "busy" sweep Dish shows while scanning / registering /
// claiming; otherwise it fills to `value` (0..1). Accent chunk over a recessed
// track, 2px radius, 3-4px tall.

import QtQuick
import Dish.Chrome

Rectangle {
    id: bar

    property bool indeterminate: true
    property real value: 0

    implicitHeight: 3
    radius: Tokens.radiusBar
    color: Theme.surface
    border.width: 1
    border.color: Theme.outline
    clip: true

    // Determinate fill.
    Rectangle {
        visible: !bar.indeterminate
        width: Math.max(0, Math.min(1, bar.value)) * parent.width
        height: parent.height
        radius: Tokens.radiusBar
        color: Theme.primary

        Behavior on width {
            NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
        }
    }

    // Indeterminate sweep: a 40%-wide chunk gliding -40% -> 100%.
    Rectangle {
        id: sweep
        visible: bar.indeterminate
        width: parent.width * 0.4
        height: parent.height
        radius: Tokens.radiusBar
        color: Theme.primary

        XAnimator on x {
            running: bar.indeterminate && bar.visible
            loops: Animation.Infinite
            from: -bar.width * 0.4
            to: bar.width
            duration: 1100
            easing.type: Easing.InOutQuad
        }
    }
}
