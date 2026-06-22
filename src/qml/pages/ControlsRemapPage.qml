// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The "Configure controls" sub-page — per-(vid,pid) raw-joystick remap for one
// slot. For a generic DirectInput pad whose button order is scrambled, this
// corrects the routing: each logical output (face buttons, d-pad, shoulders,
// sticks, triggers, …) shows which raw source it currently reads, and a
// "Press to assign" capture lets the user re-bind it by physically pressing the
// input. Plus the two stick-Y invert toggles and a reset-to-default.
//
// It targets a slot set BEFORE push (slotId/slotName). All data + actions come
// solely from the frozen App contract (docs/QML_CONTRACT.md §1b / slotRemap /
// assignSlotInput / setSlotInvert / resetSlotRemap / startInputCapture /
// stopInputCapture / rawInputCaptured): no business logic lives here. slotRemap
// is a one-shot read, so it is re-pulled after every assign/invert/reset.

// Bound so the inline AssignRow component may reference the page id without an
// unqualified-access warning, and so its required model bindings resolve.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: page
    title: qsTr("Configure controls")

    // Set by the pusher (ControllersPage) before this page becomes visible.
    property string slotId: ""
    property string slotName: ""

    // The effective remap map last read from App.slotRemap(slotId). Re-pulled on
    // load and after every assign/invert/reset (slotRemap is one-shot). Empty
    // object until the first read; an empty/no-identity slot stays {} (the
    // not-remappable note shows).
    property var remap: ({})

    // The logical output currently capturing ("" = none). Only one row captures
    // at a time; tapping "Press to assign" on a row sets this and arms the
    // bridge, the rawInputCaptured handler assigns it and clears this.
    property string capturingTarget: ""

    // True once a real remap has been read (the slot resolved to a (vid,pid)).
    // App.slotRemap returns {} for a non-remappable slot, which the page treats
    // as "no remap to show" — it renders the explanatory note instead of rows.
    readonly property bool hasRemap: remap && remap.a !== undefined

    Component.onCompleted: page.refresh()

    // Leaving the page MUST stop capture so the bridge doesn't keep streaming
    // raw inputs (contract requirement). Covers both Back and a rail switch.
    Component.onDestruction: App.stopInputCapture() // qmllint disable unqualified

    // Re-read the slot's effective remap (one-shot). Called on load and after
    // every mutation so the displayed assignments reflect the live state.
    function refresh() {
        page.remap = App.slotRemap(page.slotId); // qmllint disable unqualified
    }

    // Begin capturing for one logical output: arm the bridge and remember which
    // output the next raw input binds to. Re-pointing from another row first
    // stops the prior arm implicitly (startInputCapture re-points the filter).
    function beginCapture(target) {
        page.capturingTarget = target;
        App.startInputCapture(page.slotId); // qmllint disable unqualified
    }

    // Stop capturing without assigning (the Cancel affordance / row toggle-off).
    function cancelCapture() {
        page.capturingTarget = "";
        App.stopInputCapture(); // qmllint disable unqualified
    }

    // Render the current source of a plain button/stick/hat output: the int
    // index roles read -1 when unassigned. `kind` labels the readout.
    function indexLabel(value, kind) {
        if (value === undefined || value === null || value < 0) {
            return qsTr("Unassigned");
        }
        return qsTr("%1 %2").arg(kind).arg(value);
    }

    // Render a trigger source: {kind:"axis"|"button", index:int}. A bare/absent
    // object reads unassigned.
    function triggerLabel(obj) {
        if (!obj || obj.index === undefined || obj.index < 0) {
            return qsTr("Unassigned");
        }
        return obj.kind === "button" ? qsTr("Button %1").arg(obj.index)
                                     : qsTr("Axis %1").arg(obj.index);
    }

    // ---- Raw-input capture sink --------------------------------------------
    // While a row is capturing, the FIRST deliberate raw input for THIS slot is
    // routed to the output being edited, then capture stops and the displayed
    // assignments refresh. The bridge already filters to the capturing slot and
    // rejects idle jitter, so a resting pad never self-assigns.
    Connections {
        target: App // qmllint disable unqualified

        function onRawInputCaptured(sid, kind, index, value) {
            if (sid !== page.slotId || page.capturingTarget.length === 0) { return; }
            App.assignSlotInput(page.slotId, page.capturingTarget, kind, index); // qmllint disable unqualified
            page.capturingTarget = "";
            App.stopInputCapture(); // qmllint disable unqualified
            page.refresh();
        }
    }

    Kit.SectionHeader { label: page.slotName.length > 0 ? page.slotName
                                                        : qsTr("Configure controls") }

    // Not-remappable note: shown for a slot App.slotRemap returns {} for (an SDL
    // game controller / USB-direct synthetic / unresolvable slot). The entry on
    // ControllersPage already gates on the `remappable` role, so this is a
    // defensive fallback — but a slot can lose its identity while the page is open.
    Kit.Card {
        visible: !page.hasRemap
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 4
            Label {
                text: qsTr("Not remappable")
                color: Theme.onSurface
                font.pixelSize: 14
                font.bold: true
            }
            Label {
                text: qsTr("This controller uses its built-in mapping and can't be remapped here.")
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    // Intro hint above the rows.
    Label {
        visible: page.hasRemap
        text: qsTr("Press a control's button below, then physically press or move the input on your controller to bind it.")
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        width: parent ? parent.width : implicitWidth
    }

    // ── Buttons ──────────────────────────────────────────────────────────────
    Kit.Card {
        visible: page.hasRemap
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 6
            Kit.SectionHeader { label: qsTr("Buttons") }

            AssignRow { rowLabel: qsTr("A (bottom)"); target: "a"
                        current: page.indexLabel(page.remap.a, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("B (right)"); target: "b"
                        current: page.indexLabel(page.remap.b, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("X (left)"); target: "x"
                        current: page.indexLabel(page.remap.x, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Y (top)"); target: "y"
                        current: page.indexLabel(page.remap.y, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Left shoulder"); target: "leftShoulder"
                        current: page.indexLabel(page.remap.leftShoulder, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Right shoulder"); target: "rightShoulder"
                        current: page.indexLabel(page.remap.rightShoulder, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Back"); target: "back"
                        current: page.indexLabel(page.remap.back, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Start"); target: "start"
                        current: page.indexLabel(page.remap.start, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Left thumb"); target: "leftThumb"
                        current: page.indexLabel(page.remap.leftThumb, qsTr("Button")) }
            AssignRow { rowLabel: qsTr("Right thumb"); target: "rightThumb"
                        current: page.indexLabel(page.remap.rightThumb, qsTr("Button")) }
        }
    }

    // ── D-pad ────────────────────────────────────────────────────────────────
    // Each direction may route to a button OR (a hat-kind capture) to the hat.
    // The current readout shows the per-direction button index when one is set,
    // else the shared hat index if a hat is mapped.
    Kit.Card {
        visible: page.hasRemap
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 6
            Kit.SectionHeader { label: qsTr("D-pad") }

            AssignRow { rowLabel: qsTr("Up"); target: "dpadUp"
                        current: page.dpadLabel(page.remap.dpadUp) }
            AssignRow { rowLabel: qsTr("Down"); target: "dpadDown"
                        current: page.dpadLabel(page.remap.dpadDown) }
            AssignRow { rowLabel: qsTr("Left"); target: "dpadLeft"
                        current: page.dpadLabel(page.remap.dpadLeft) }
            AssignRow { rowLabel: qsTr("Right"); target: "dpadRight"
                        current: page.dpadLabel(page.remap.dpadRight) }
        }
    }

    // ── Sticks ───────────────────────────────────────────────────────────────
    Kit.Card {
        visible: page.hasRemap
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 6
            Kit.SectionHeader { label: qsTr("Sticks") }

            AssignRow { rowLabel: qsTr("Left stick X"); target: "leftStickX"
                        current: page.indexLabel(page.remap.leftStickX, qsTr("Axis")) }
            AssignRow { rowLabel: qsTr("Left stick Y"); target: "leftStickY"
                        current: page.indexLabel(page.remap.leftStickY, qsTr("Axis")) }
            AssignRow { rowLabel: qsTr("Right stick X"); target: "rightStickX"
                        current: page.indexLabel(page.remap.rightStickX, qsTr("Axis")) }
            AssignRow { rowLabel: qsTr("Right stick Y"); target: "rightStickY"
                        current: page.indexLabel(page.remap.rightStickY, qsTr("Axis")) }

            Kit.LabeledSwitch {
                Layout.fillWidth: true
                label: qsTr("Invert Left-Y")
                checked: page.remap.invertLeftY === true
                onToggled: {
                    App.setSlotInvert(page.slotId, "leftY", checked); // qmllint disable unqualified
                    page.refresh();
                }
            }
            Kit.LabeledSwitch {
                Layout.fillWidth: true
                label: qsTr("Invert Right-Y")
                checked: page.remap.invertRightY === true
                onToggled: {
                    App.setSlotInvert(page.slotId, "rightY", checked); // qmllint disable unqualified
                    page.refresh();
                }
            }
        }
    }

    // ── Triggers ─────────────────────────────────────────────────────────────
    // A trigger captures from either an axis (analogue) or a button (digital
    // full-scale on press); the source kind tags from the captured input kind.
    Kit.Card {
        visible: page.hasRemap
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 6
            Kit.SectionHeader { label: qsTr("Triggers") }

            AssignRow { rowLabel: qsTr("Left trigger"); target: "leftTrigger"
                        current: page.triggerLabel(page.remap.leftTrigger) }
            AssignRow { rowLabel: qsTr("Right trigger"); target: "rightTrigger"
                        current: page.triggerLabel(page.remap.rightTrigger) }
        }
    }

    // ── Reset ────────────────────────────────────────────────────────────────
    Kit.OutlineButton {
        visible: page.hasRemap
        text: qsTr("Reset to default")
        onClicked: {
            // Drop the override and refresh so the rows fall back to the default
            // DirectInput layout. Any in-flight capture is stopped first.
            page.cancelCapture();
            App.resetSlotRemap(page.slotId); // qmllint disable unqualified
            page.refresh();
        }
    }

    // The d-pad current readout: a per-direction button index wins; else the
    // shared hat (when one is mapped); else unassigned. hatIndex is a sibling
    // map field shared by all four directions.
    function dpadLabel(dirValue) {
        if (dirValue !== undefined && dirValue !== null && dirValue >= 0) {
            return qsTr("Button %1").arg(dirValue);
        }
        if (page.remap.hatIndex !== undefined && page.remap.hatIndex >= 0) {
            return qsTr("Hat %1").arg(page.remap.hatIndex);
        }
        return qsTr("Unassigned");
    }

    // ---- Inline assign row -------------------------------------------------
    // One output's row: a label, its current source readout, and a capture
    // button. While THIS row is capturing it shows a "Press an input…" prompt
    // and a Cancel; otherwise a "Press to assign" button (disabled while
    // ANOTHER row captures, so only one assigns at a time).
    component AssignRow: RowLayout {
        id: assignRow
        property string rowLabel: ""
        property string target: ""
        property string current: ""

        readonly property bool capturing: page.capturingTarget === assignRow.target
        readonly property bool otherCapturing: page.capturingTarget.length > 0
                                                && !assignRow.capturing

        Layout.fillWidth: true
        spacing: 10

        Label {
            text: assignRow.rowLabel
            color: Theme.onSurface
            font.pixelSize: 13
            Layout.preferredWidth: 120
        }

        Label {
            // While capturing this row, prompt instead of the stale source.
            text: assignRow.capturing
                  ? qsTr("Press a button or move an axis…")
                  : assignRow.current
            color: assignRow.capturing ? Theme.primary : Theme.muted
            font.pixelSize: 12
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Kit.OutlineButton {
            visible: assignRow.capturing
            text: qsTr("Cancel")
            onClicked: page.cancelCapture()
        }
        Kit.KitButton {
            visible: !assignRow.capturing
            text: qsTr("Press to assign")
            // Disabled while another row owns the capture so two rows can't arm
            // at once (the bridge filters to one slot, not one output).
            enabled: !assignRow.otherCapturing
            onClicked: page.beginCapture(assignRow.target)
        }
    }
}
