// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The apply overlay's step rows: one row per REAL async action, never a
// spinner. The Connection step alone can sit for 20s while Windows hands the
// device over — that is precisely why it is a step with a name and a caption
// and not an undifferentiated wait.
//
// `steps` is a plain array of
//   { label: string, meta: string, state: "done"|"active"|"pending"|"failed" }
// The caller owns the copy; this component owns the markers and the a11y
// announcement. Two callers (the wizard's Review page and Configure binding),
// which is why it is here and not inlined twice.

// Bound: the delegate reads the outer `list` id alongside its required
// modelData, so static resolution needs bound component behavior.
pragma ComponentBehavior: Bound

import QtQuick
import Dish.Chrome

Column {
    id: list

    property var steps: []

    spacing: 0

    // The spoken state word. Kept out of the delegate so the four strings live
    // in one place and the marker glyphs cannot drift from what is announced.
    function stateLabel(s) {
        if (s === "done")
            return qsTr("done");
        if (s === "active")
            return qsTr("in progress");
        if (s === "failed")
            return qsTr("failed");
        return qsTr("pending");
    }

    Repeater {
        model: list.steps

        delegate: Item {
            id: stepRow

            required property var modelData

            // NOT named `state`: Item.state is a built-in string property that
            // drives State activation, so binding it to "done" would hunt for a
            // State of that name and warn on every row.
            readonly property string stepState: stepRow.modelData.state !== undefined
                                              ? stepRow.modelData.state : "pending"
            readonly property string stepLabel: stepRow.modelData.label !== undefined
                                              ? stepRow.modelData.label : ""
            readonly property string stepMeta: stepRow.modelData.meta !== undefined
                                             ? stepRow.modelData.meta : ""

            width: list.width
            implicitWidth: 16 + Tokens.s6 + labelText.implicitWidth + Tokens.s5
                           + metaText.implicitWidth
            height: Math.max(16, labelText.implicitHeight, metaText.implicitHeight)
                    + 2 * Tokens.s3

            // A step that has not started yet is the one legal information dim:
            // it is a control-shaped placeholder, not a fact the user must read.
            opacity: stepRow.stepState === "pending" ? Tokens.disabledOpacity : 1.0

            Accessible.role: Accessible.StaticText
            Accessible.name: stepRow.stepMeta.length > 0
                             ? qsTr("%1 — %2 · %3").arg(stepRow.stepLabel)
                                                   .arg(list.stateLabel(stepRow.stepState))
                                                   .arg(stepRow.stepMeta)
                             : qsTr("%1 — %2").arg(stepRow.stepLabel)
                                              .arg(list.stateLabel(stepRow.stepState))

            Item {
                id: markerCell
                width: 16
                height: 16
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter

                Text {
                    anchors.centerIn: parent
                    visible: stepRow.stepState === "done"
                    text: "✓"
                    color: Theme.success
                    font.pixelSize: Tokens.textSummary
                }

                Text {
                    anchors.centerIn: parent
                    visible: stepRow.stepState === "failed"
                    text: "✕"
                    color: Theme.error
                    font.pixelSize: Tokens.textSummary
                }

                // The active ring: an outline circle with an accent top arc,
                // spun by an animator. Reduced motion leaves the arc parked —
                // the arc alone still reads as "this one".
                Canvas {
                    id: ring
                    visible: stepRow.stepState === "active"
                    anchors.centerIn: parent
                    width: 12
                    height: 12

                    onPaint: {
                        const ctx = getContext("2d");
                        ctx.reset();
                        const r = width / 2 - 1;
                        ctx.lineWidth = 2;
                        const o = Theme.outline;
                        ctx.strokeStyle = Qt.rgba(o.r, o.g, o.b, o.a);
                        ctx.beginPath();
                        ctx.arc(width / 2, height / 2, r, 0, Math.PI * 2);
                        ctx.stroke();
                        const p = Theme.primary;
                        ctx.strokeStyle = Qt.rgba(p.r, p.g, p.b, p.a);
                        ctx.beginPath();
                        ctx.arc(width / 2, height / 2, r, -Math.PI / 2, 0);
                        ctx.stroke();
                    }

                    RotationAnimator on rotation {
                        running: ring.visible && !Tokens.reducedMotion
                        loops: Animation.Infinite
                        from: 0
                        to: 360
                        duration: Tokens.durBusy
                    }

                    Connections {
                        target: Theme
                        function onPaletteChanged() { ring.requestPaint(); }
                    }
                }

                Rectangle {
                    anchors.centerIn: parent
                    visible: stepRow.stepState === "pending"
                    width: 8
                    height: 8
                    radius: width / 2
                    color: "transparent"
                    border.width: 1
                    border.color: Theme.muted
                }
            }

            Text {
                id: labelText
                anchors.left: markerCell.right
                anchors.leftMargin: Tokens.s6
                anchors.right: metaText.left
                anchors.rightMargin: Tokens.s5
                anchors.verticalCenter: parent.verticalCenter
                text: stepRow.stepLabel
                elide: Text.ElideRight
                color: stepRow.stepState === "failed" ? Theme.error : Theme.onSurface
                font.pixelSize: Tokens.textBase
            }

            Text {
                id: metaText
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: stepRow.stepMeta
                color: Theme.mutedStrong
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
                font.letterSpacing: Tokens.sectionLetterSpacing
                font.capitalization: Font.AllUppercase
            }
        }
    }
}
