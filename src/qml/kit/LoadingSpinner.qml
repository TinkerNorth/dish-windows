// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// A centered busy indicator with a caption — the shared "loading" state so pages
// stop hand-rolling a bare BusyIndicator + Label. Drop it inside a Kit.Card or a
// Kit.Page: it sizes to its content and centers, with the spinner themed to
// Theme.primary. Set `text` for a caption under the spinner; bind `running` to
// gate it (defaults true).
//
// Two documented callers: the type-catalog loader in the wizard's type step (a
// guessed default is worse than a wait) and the apply overlay's step spinner.
// It is NEVER shipped bare — the caption names what is being waited on, because
// "never a bare spinner" is a rule, not a preference.
//
// Under Tokens.reducedMotion the ring stops and simply renders — a static ring
// still says "busy" beside its caption.
//
// Usage:
//   Kit.LoadingSpinner { running: App.busy; text: qsTr("Connecting…") }

// Bound component behavior so the ring's Repeater delegate can reference the
// outer `spinner` id (its `running` flag) and its own `required` index
// statically — keeps binding resolution static and qmllint quiet.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

ColumnLayout {
    id: spinner

    // The caption shown under the spinner (hidden when empty).
    property string text: ""
    // Whether the indicator animates. Bind to a busy flag; defaults on.
    property bool running: true

    spacing: Tokens.s6

    BusyIndicator {
        running: spinner.running
        // Hide entirely when not running so the column collapses (no stray gap).
        visible: spinner.running
        implicitWidth: Tokens.s11
        implicitHeight: Tokens.s11
        Layout.alignment: Qt.AlignHCenter

        // Theme the Basic-style indicator to the brand primary — the default
        // contentItem paints fixed greys that clash with the Mica surface.
        contentItem: Item {
            implicitWidth: Tokens.s11
            implicitHeight: Tokens.s11

            Repeater {
                model: 8
                delegate: Rectangle {
                    id: dot
                    required property int index
                    readonly property real angle: dot.index / 8 * 2 * Math.PI
                    width: 4
                    height: 4
                    radius: width / 2
                    color: Theme.primary
                    // Trailing fade around the ring so the spin reads directional.
                    opacity: (dot.index + 1) / 8
                    x: 16 + Math.cos(dot.angle) * 12 - width / 2
                    y: 16 + Math.sin(dot.angle) * 12 - height / 2
                }
            }

            // One continuous rotation while running; stops cleanly when hidden
            // or when the OS asks for reduced motion.
            RotationAnimation on rotation {
                running: spinner.running && !Tokens.reducedMotion
                from: 0
                to: 360
                duration: Tokens.durBusy
                loops: Animation.Infinite
            }
        }
    }

    Label {
        text: spinner.text
        visible: spinner.text.length > 0
        color: Theme.mutedStrong
        font.pixelSize: Tokens.textSummary
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        Layout.alignment: Qt.AlignHCenter
        Layout.maximumWidth: 280
    }
}
