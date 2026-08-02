// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Thin progress bar (design-system ProgressBar): with `indeterminate` it runs
// the accent "busy" sweep Dish shows while scanning / registering / claiming;
// otherwise it fills to `value` (0..1).
//
// The track is Theme.surfaceDim, NOT Theme.surface: every busy bar in the app
// sits inside a Card, so a surface-coloured track is invisible and the bar reads
// as a floating chunk with no extent — destroying the one thing an indeterminate
// bar communicates.
//
// The height is 3 and is NOT caller-settable: a design system with a
// caller-settable bar height has no bar height. Bind width, never height.

import QtQuick
import Dish.Chrome

Rectangle {
    id: bar

    property bool indeterminate: true
    property real value: 0

    // Reduced motion turns the sweep into a static filled track — the bar still
    // says "busy", it just stops moving.
    readonly property bool sweeping: bar.indeterminate && !Tokens.reducedMotion

    implicitHeight: 3
    radius: Tokens.radiusBar
    color: Theme.surfaceDim
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
            enabled: !Tokens.reducedMotion
            NumberAnimation { duration: Tokens.durNormal; easing.type: Easing.OutQuad }
        }
    }

    // Reduced-motion indeterminate: a full-width wash, no animator at all.
    Rectangle {
        visible: bar.indeterminate && Tokens.reducedMotion
        anchors.fill: parent
        radius: Tokens.radiusBar
        color: Theme.primaryFill
    }

    // Indeterminate sweep: a 40%-wide chunk gliding -40% -> 100%. Gated on
    // `visible` so an off-screen page does not keep an animator alive.
    Rectangle {
        id: sweep
        visible: bar.sweeping
        width: parent.width * 0.4
        height: parent.height
        radius: Tokens.radiusBar
        color: Theme.primary

        XAnimator on x {
            running: bar.sweeping && bar.visible
            loops: Animation.Infinite
            from: -bar.width * 0.4
            to: bar.width
            duration: Tokens.durBusy
            easing.type: Easing.InOutQuad
        }
    }
}
