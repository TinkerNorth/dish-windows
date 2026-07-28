// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The "Configure controls" sub-page (design frame 13) — per-(vid,pid)
// raw-joystick remap for one slot. A capture banner sits on top while
// listening; below it a two-column grid: Buttons & D-pad on the left, Sticks &
// Triggers plus the invert toggles on the right. Each assign row is itself the
// capture affordance — click it, then physically press the input to bind it.
//
// It targets a slot set BEFORE push (slotId/slotName). All data + actions come
// solely from the frozen App contract (slotRemap / assignSlotInput /
// setSlotInvert / resetSlotRemap / startInputCapture / stopInputCapture /
// rawInputCaptured): no business logic lives here. slotRemap is a one-shot
// read, so it is re-pulled after every mutation.

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

    // Set by the pusher (ControllersPage) before this page becomes visible.
    property string slotId: ""
    property string slotName: ""

    readonly property string headerTitle: qsTr("Configure controls")
    readonly property string headerSub: page.slotName.length > 0
        ? qsTr("%1 · raw DirectInput remap").arg(page.slotName)
        : qsTr("Raw DirectInput remap")

    // The effective remap map last read from App.slotRemap(slotId). Re-pulled on
    // load and after every assign/invert/reset (slotRemap is one-shot). Empty
    // object until the first read; an empty/no-identity slot stays {} (the
    // not-remappable note shows).
    property var remap: ({})

    // The logical output currently capturing ("" = none) and its display label
    // for the banner. Only one row captures at a time.
    property string capturingTarget: ""
    property string capturingLabel: ""

    // True once a real remap has been read (the slot resolved to a (vid,pid)).
    readonly property bool hasRemap: remap && remap.a !== undefined

    Component.onCompleted: page.refresh()

    // Leaving the page MUST stop capture so the bridge doesn't keep streaming
    // raw inputs (contract requirement). Covers both Back and a rail switch.
    Component.onDestruction: App.stopInputCapture() // qmllint disable unqualified

    function refresh() {
        page.remap = App.slotRemap(page.slotId); // qmllint disable unqualified
    }

    // Begin capturing for one logical output: arm the bridge and remember which
    // output the next raw input binds to. Re-pointing from another row first
    // stops the prior arm implicitly (startInputCapture re-points the filter).
    function beginCapture(target, label) {
        page.capturingTarget = target;
        page.capturingLabel = label;
        App.startInputCapture(page.slotId); // qmllint disable unqualified
    }

    function cancelCapture() {
        page.capturingTarget = "";
        page.capturingLabel = "";
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

    // Render a trigger source: {kind:"axis"|"button", index:int}.
    function triggerLabel(obj) {
        if (!obj || obj.index === undefined || obj.index < 0) {
            return qsTr("Unassigned");
        }
        return obj.kind === "button" ? qsTr("Button %1 · digital").arg(obj.index)
                                     : qsTr("Axis %1 · analog").arg(obj.index);
    }

    // The d-pad current readout: a per-direction button index wins; else the
    // shared hat (when one is mapped); else unassigned.
    function dpadLabel(dirValue) {
        if (dirValue !== undefined && dirValue !== null && dirValue >= 0) {
            return qsTr("Button %1").arg(dirValue);
        }
        if (page.remap.hatIndex !== undefined && page.remap.hatIndex >= 0) {
            return qsTr("Hat %1").arg(page.remap.hatIndex);
        }
        return qsTr("Unassigned");
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
            page.cancelCapture();
            page.refresh();
        }
    }

    // ---- Inline assign row -------------------------------------------------
    // One output's row (design AssignRow): label left, mono value chip right;
    // the WHOLE row is the capture affordance. While this row captures, both
    // ends light accent; another row capturing re-points on click.
    component AssignRow: Item {
        id: assignRow
        property string rowLabel: ""
        property string target: ""
        property string current: ""

        readonly property bool capturing: page.capturingTarget === assignRow.target

        Layout.fillWidth: true
        implicitHeight: Math.max(labelText.implicitHeight, chip.implicitHeight) + 8

        Label {
            id: labelText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: assignRow.rowLabel
            color: assignRow.capturing ? Theme.primary : Theme.onSurface
            font.pixelSize: Tokens.textSummary
            font.weight: assignRow.capturing ? Font.DemiBold : Font.Normal
        }

        Rectangle {
            id: chip
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth: chipText.implicitWidth + 16
            implicitHeight: chipText.implicitHeight + 6
            radius: Tokens.radiusChip
            color: Theme.surfaceDim
            border.width: 1
            border.color: assignRow.capturing ? Theme.primary : Theme.outline

            Label {
                id: chipText
                anchors.centerIn: parent
                text: assignRow.capturing ? qsTr("waiting for input…") : assignRow.current
                color: assignRow.capturing ? Theme.primary : Theme.muted
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
            }
        }

        MouseArea {
            anchors.fill: parent
            cursorShape: Qt.PointingHandCursor
            onClicked: assignRow.capturing
                       ? page.cancelCapture()
                       : page.beginCapture(assignRow.target, assignRow.rowLabel)
        }
    }

    // ---- Body ---------------------------------------------------------------
    ColumnLayout {
        width: Math.min(760, parent.width)
        spacing: Tokens.s5

        // Not-remappable note: defensive fallback — the ControllersPage entry
        // already gates on the `remappable` role, but a slot can lose its
        // identity while the page is open.
        Rectangle {
            visible: !page.hasRemap
            Layout.fillWidth: true
            implicitHeight: notRemapCol.implicitHeight + 24
            radius: Tokens.radiusCard
            color: Theme.surface
            border.width: 1
            border.color: Theme.outline

            ColumnLayout {
                id: notRemapCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Tokens.s7
                anchors.rightMargin: Tokens.s7
                spacing: Tokens.s2
                Label {
                    text: qsTr("Not remappable")
                    color: Theme.onSurface
                    font.pixelSize: 14
                    font.bold: true
                }
                Label {
                    text: qsTr("This controller uses its built-in mapping and can't be remapped here.")
                    color: Theme.muted
                    font.pixelSize: Tokens.textSummary
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // Capture banner (design): accent-bordered card with the busy sweep and
        // the output being bound; Stop capture on the right. Idle: a quiet hint.
        Rectangle {
            visible: page.hasRemap
            Layout.fillWidth: true
            implicitHeight: bannerRow.implicitHeight + 24
            radius: Tokens.radiusCard
            color: Theme.surface
            border.width: page.capturingTarget.length > 0 ? 2 : 1
            border.color: page.capturingTarget.length > 0 ? Theme.primary : Theme.outline

            RowLayout {
                id: bannerRow
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: Tokens.s7
                anchors.rightMargin: Tokens.s7
                spacing: Tokens.s7

                Kit.DishProgressBar {
                    visible: page.capturingTarget.length > 0
                    indeterminate: true
                    Layout.preferredWidth: 80
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: page.capturingTarget.length > 0 ? qsTr("Listening for input")
                                                              : qsTr("Click a control to rebind it")
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.DemiBold
                    }
                    Label {
                        textFormat: Text.StyledText
                        text: page.capturingTarget.length > 0
                              ? qsTr("Press the input on your controller to assign <b>%1</b>. Idle jitter is ignored — a resting pad never self-assigns.").arg(page.capturingLabel)
                              : qsTr("Then physically press or move the input on your controller to bind it.")
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                Kit.OutlineButton {
                    visible: page.capturingTarget.length > 0
                    text: qsTr("Stop capture")
                    onClicked: page.cancelCapture()
                }
            }
        }

        // Two-column grid: Buttons & D-pad left; Sticks & Triggers + inverts right.
        GridLayout {
            visible: page.hasRemap
            columns: page.width < 700 ? 1 : 2
            columnSpacing: Tokens.s6
            rowSpacing: Tokens.s6
            Layout.fillWidth: true

            Rectangle {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                implicitHeight: buttonsCol.implicitHeight + 24
                radius: Tokens.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.outline

                ColumnLayout {
                    id: buttonsCol
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Tokens.s7
                    anchors.rightMargin: Tokens.s7
                    spacing: 2

                    Kit.Eyebrow { text: qsTr("Buttons & D-pad"); mutedTone: true
                                  Layout.bottomMargin: Tokens.s3 }

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
                    AssignRow { rowLabel: qsTr("D-pad up"); target: "dpadUp"
                                current: page.dpadLabel(page.remap.dpadUp) }
                    AssignRow { rowLabel: qsTr("D-pad down"); target: "dpadDown"
                                current: page.dpadLabel(page.remap.dpadDown) }
                    AssignRow { rowLabel: qsTr("D-pad left"); target: "dpadLeft"
                                current: page.dpadLabel(page.remap.dpadLeft) }
                    AssignRow { rowLabel: qsTr("D-pad right"); target: "dpadRight"
                                current: page.dpadLabel(page.remap.dpadRight) }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignTop
                spacing: Tokens.s6

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: sticksCol.implicitHeight + 24
                    radius: Tokens.radiusCard
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.outline

                    ColumnLayout {
                        id: sticksCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Tokens.s7
                        anchors.rightMargin: Tokens.s7
                        spacing: 2

                        Kit.Eyebrow { text: qsTr("Sticks & triggers"); mutedTone: true
                                      Layout.bottomMargin: Tokens.s3 }

                        AssignRow { rowLabel: qsTr("Left stick X"); target: "leftStickX"
                                    current: page.indexLabel(page.remap.leftStickX, qsTr("Axis")) }
                        AssignRow { rowLabel: qsTr("Left stick Y"); target: "leftStickY"
                                    current: page.indexLabel(page.remap.leftStickY, qsTr("Axis")) }
                        AssignRow { rowLabel: qsTr("Right stick X"); target: "rightStickX"
                                    current: page.indexLabel(page.remap.rightStickX, qsTr("Axis")) }
                        AssignRow { rowLabel: qsTr("Right stick Y"); target: "rightStickY"
                                    current: page.indexLabel(page.remap.rightStickY, qsTr("Axis")) }
                        AssignRow { rowLabel: qsTr("Left trigger"); target: "leftTrigger"
                                    current: page.triggerLabel(page.remap.leftTrigger) }
                        AssignRow { rowLabel: qsTr("Right trigger"); target: "rightTrigger"
                                    current: page.triggerLabel(page.remap.rightTrigger) }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: invertCol.implicitHeight + 16
                    radius: Tokens.radiusCard
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.outline

                    ColumnLayout {
                        id: invertCol
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.leftMargin: Tokens.s7
                        anchors.rightMargin: Tokens.s7
                        spacing: 0

                        Kit.LabeledSwitch {
                            Layout.fillWidth: true
                            label: qsTr("Invert left stick Y")
                            checked: page.remap.invertLeftY === true
                            onToggled: {
                                App.setSlotInvert(page.slotId, "leftY", checked); // qmllint disable unqualified
                                page.refresh();
                            }
                        }
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 1
                            color: Theme.outline
                        }
                        Kit.LabeledSwitch {
                            Layout.fillWidth: true
                            label: qsTr("Invert right stick Y")
                            checked: page.remap.invertRightY === true
                            onToggled: {
                                App.setSlotInvert(page.slotId, "rightY", checked); // qmllint disable unqualified
                                page.refresh();
                            }
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.s5

                    Label {
                        text: qsTr("Stored for this controller model — applies on the next report, no re-attach.")
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Kit.OutlineButton {
                        text: qsTr("Reset to defaults")
                        onClicked: {
                            page.cancelCapture();
                            App.resetSlotRemap(page.slotId); // qmllint disable unqualified
                            page.refresh();
                        }
                    }
                }
            }
        }
    }
}
