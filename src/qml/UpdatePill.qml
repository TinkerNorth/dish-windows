// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The updater's one piece of chrome: a caption-strip control that exists only
// while there is something to say. Disabled, idle, up-to-date, checking and
// failed render NOTHING — a check is silent by requirement and a failure is a
// Settings matter — so a build that never sees an update is visually identical
// to one with no updater at all.
//
// It is a CLIENT CARVE-OUT: WindowTitleBar publishes its rect (empty while
// hidden) to ChromeBridge, or the native resolver would answer HTCAPTION over
// it and a press would start a window drag instead of opening the popover.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome
import "kit" as Kit

AbstractButton {
    id: pill

    // The four phases with something to show. Everything else collapses the
    // item, which also collapses the Row cell it sits in.
    visible: App.updatePhase === "available" || App.updatePhase === "downloading"
             || App.updatePhase === "verifying" || App.updatePhase === "ready"

    readonly property bool busy: App.updatePhase === "downloading"
                                 || App.updatePhase === "verifying"
    readonly property bool ready: App.updatePhase === "ready"

    // Canvas 2D parses a stringified colour as #RRGGBBAA, so only an OPAQUE
    // Theme role may be handed to it this way — muted, primary and onSurface
    // are opaque by construction (kit rule C5).
    readonly property color glyphColor: pill.ready ? Theme.onSurface
                                      : pill.busy ? Theme.primary
                                      : Theme.muted

    readonly property string stateText: pill.ready
        ? qsTr("Dish %1 is ready to install.").arg(App.updateVersion)
        : pill.busy ? qsTr("%1 of %2").arg(App.updateReceivedText).arg(App.updateTotalText)
        : qsTr("Update available: Dish %1").arg(App.updateVersion)

    hoverEnabled: true
    focusPolicy: Qt.StrongFocus

    Accessible.role: Accessible.Button
    Accessible.name: pill.stateText

    onClicked: popover.opened ? popover.close() : popover.open()

    onGlyphColorChanged: glyphCanvas.requestPaint()

    background: Rectangle {
        color: pill.hovered ? Theme.primaryHover : "transparent"
    }

    contentItem: Item {
        id: glyphCell

        // The arrow-into-tray mark: a shaft with a head, over an open tray.
        // Redrawn on every colour move because Canvas does not rebind.
        Canvas {
            id: glyphCanvas
            anchors.fill: parent
            onPaint: {
                var ctx = getContext("2d");
                ctx.reset();
                ctx.strokeStyle = String(pill.glyphColor);
                ctx.lineWidth = 1.4;
                ctx.lineCap = "round";
                ctx.lineJoin = "round";
                var cx = width / 2, cy = height / 2;
                ctx.beginPath();
                ctx.moveTo(cx, cy - 6);
                ctx.lineTo(cx, cy + 1);
                ctx.moveTo(cx - 3.5, cy - 2.5);
                ctx.lineTo(cx, cy + 1);
                ctx.lineTo(cx + 3.5, cy - 2.5);
                ctx.stroke();
                ctx.beginPath();
                ctx.moveTo(cx - 5, cy + 2.5);
                ctx.lineTo(cx - 5, cy + 5);
                ctx.lineTo(cx + 5, cy + 5);
                ctx.lineTo(cx + 5, cy + 2.5);
                ctx.stroke();
            }
            Connections {
                target: Theme
                function onPaletteChanged() { glyphCanvas.requestPaint(); }
            }
        }

        // The one arrival moment: a single rise plus one scale pulse when the
        // staged build becomes ready, then static forever. Never loops.
        SequentialAnimation {
            id: arriveAnimation
            running: false
            NumberAnimation {
                target: glyphCell
                property: "y"
                from: Tokens.s2
                to: 0
                duration: Tokens.durFast
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: glyphCell
                property: "scale"
                from: 1.0
                to: 1.12
                duration: Tokens.durFast
                easing.type: Easing.OutQuad
            }
            NumberAnimation {
                target: glyphCell
                property: "scale"
                from: 1.12
                to: 1.0
                duration: Tokens.durNormal
                easing.type: Easing.OutBack
            }
        }
    }

    onReadyChanged: {
        if (pill.ready && !Tokens.reducedMotion)
            arriveAnimation.restart();
    }

    // Determinate while downloading, an indeterminate sweep while the finished
    // file is re-hashed: verifying has no progress to report and a bar frozen
    // at 100 percent would be a lie.
    Rectangle {
        id: underline
        visible: pill.busy
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 2
        color: Theme.surfaceDim
        clip: true

        Rectangle {
            visible: App.updateProgress >= 0
            height: parent.height
            color: Theme.primary
            width: underline.width * Math.max(0, Math.min(1, App.updateProgress))

            Behavior on width {
                enabled: !Tokens.reducedMotion
                NumberAnimation { duration: Tokens.durNormal; easing.type: Easing.OutQuad }
            }
        }

        // A separate item, not the same bar re-purposed: a determinate fill
        // whose x was left mid-sweep would render as a floating chunk.
        Rectangle {
            id: underlineSweep
            visible: App.updateProgress < 0
            height: parent.height
            width: underline.width / 3
            color: Theme.primary

            SequentialAnimation on x {
                running: underlineSweep.visible && !Tokens.reducedMotion
                loops: Animation.Infinite
                NumberAnimation {
                    from: -underlineSweep.width
                    to: underline.width
                    duration: Tokens.durBusy
                    easing.type: Easing.InOutQuad
                }
            }
        }
    }

    // 7px, smaller than the 10px StatusDot default: this is a badge on a glyph,
    // not a status row's dot. Warning tone when the running build is no longer
    // supported.
    Kit.StatusDot {
        visible: pill.ready
        token: App.updateRequired ? "warning" : "primary"
        width: 7
        height: 7
        x: pill.width / 2 + Tokens.s3
        y: pill.height / 2 - Tokens.s5
    }

    // Declared, never attached — see DishToolTip in QML_UI_KIT.md.
    Kit.DishToolTip {
        id: pillTip
        visible: pill.hovered && !popover.opened
        delay: 500
        text: pill.stateText
        y: pill.height + Tokens.s2
    }

    UpdatePopover {
        id: popover
        y: pill.height + Tokens.s1
        x: pill.width - width
    }
}
