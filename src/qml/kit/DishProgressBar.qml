// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The track is Theme.surfaceDim, NOT Theme.surface: every busy bar sits inside
// a Card, where a surface-coloured track is invisible and the bar reads as a
// floating chunk with no extent. Bind width, never height.

import QtQuick
import Dish.Chrome

Rectangle {
    id: bar

    property bool indeterminate: true
    property real value: 0

    readonly property bool sweeping: bar.indeterminate && !Tokens.reducedMotion

    implicitHeight: 3
    radius: Tokens.radiusBar
    color: Theme.surfaceDim
    border.width: 1
    border.color: Theme.outline
    clip: true

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

    // Reduced motion: a static full-width wash, no animator at all.
    Rectangle {
        visible: bar.indeterminate && Tokens.reducedMotion
        anchors.fill: parent
        radius: Tokens.radiusBar
        color: Theme.primaryFill
    }

    // Gated on `visible` so an off-screen page does not keep an animator alive.
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
