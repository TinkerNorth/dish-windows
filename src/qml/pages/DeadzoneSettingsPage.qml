// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Dead zones & motion detail (design frame 14): one card per device — name +
// gyro chip, stick/trigger dead-zone sliders side by side, and the motion
// toggle (or the no-gyro note) under a hairline — capped at a readable 640px
// measure.
//
// The sliders PRESENT percentages of the full axis travel (the design's 0-30%
// scale) while the persisted values stay in the raw units the input processor
// consumes (stick flat 0..32767, trigger flat 0..255) — the mapping lives here
// so neither the store schema nor the hot path changes.

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: deadzonePage

    readonly property string headerTitle: qsTr("Dead zones & motion")
    readonly property string headerSub: qsTr("Per-device tuning — applied live")

    // Raw-axis full ranges the percentages map over.
    readonly property real stickRange: 32767
    readonly property real triggerRange: 255
    readonly property int percentMax: 30

    function toPercent(raw, range) { return Math.round(raw / range * 100); }
    function fromPercent(pct, range) { return Math.round(pct / 100 * range); }

    // Device rows from App.deadzoneDevices(): { id, name, hasGyro, stickFlat,
    // triggerFlat, forwardMotion }. Re-pulled whenever the rows move via
    // App.deadzonesChanged (a device attached/detached, or a set landed).
    property var deviceModel: App.deadzoneDevices()

    Connections {
        target: App
        function onDeadzonesChanged() { deadzonePage.deviceModel = App.deadzoneDevices(); }
    }

    ColumnLayout {
        width: Math.min(640, parent.width)
        spacing: Tokens.s6

        Label {
            visible: deadzonePage.deviceModel.length === 0
            text: qsTr("Connect a controller to adjust its dead zones.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        Repeater {
            model: deadzonePage.deviceModel
            delegate: Rectangle {
                id: deviceCard
                required property var modelData

                Layout.fillWidth: true
                implicitHeight: cardBody.implicitHeight + 28
                radius: Tokens.radiusCard
                color: Theme.surface
                border.width: 1
                border.color: Theme.outline

                ColumnLayout {
                    id: cardBody
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    anchors.leftMargin: Tokens.s8
                    anchors.rightMargin: Tokens.s8
                    spacing: Tokens.s3

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: Tokens.s5
                        Label {
                            text: deviceCard.modelData.name.length > 0
                                  ? deviceCard.modelData.name : deviceCard.modelData.id
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Kit.CapabilityChip {
                            present: deviceCard.modelData.hasGyro === true
                            text: deviceCard.modelData.hasGyro === true ? qsTr("Gyro")
                                                                        : qsTr("No gyro")
                        }
                    }

                    GridLayout {
                        columns: 2
                        columnSpacing: Tokens.s9 + 4
                        Layout.fillWidth: true
                        Layout.topMargin: Tokens.s3

                        Kit.SliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Stick dead zone")
                            maxValue: deadzonePage.percentMax
                            value: deadzonePage.toPercent(deviceCard.modelData.stickFlat,
                                                          deadzonePage.stickRange)
                            onCommitted: pct => App.setDeadzones(
                                deviceCard.modelData.id,
                                deadzonePage.fromPercent(pct, deadzonePage.stickRange),
                                deviceCard.modelData.triggerFlat)
                        }
                        Kit.SliderRow {
                            Layout.fillWidth: true
                            label: qsTr("Trigger dead zone")
                            maxValue: deadzonePage.percentMax
                            value: deadzonePage.toPercent(deviceCard.modelData.triggerFlat,
                                                          deadzonePage.triggerRange)
                            onCommitted: pct => App.setDeadzones(
                                deviceCard.modelData.id,
                                deviceCard.modelData.stickFlat,
                                deadzonePage.fromPercent(pct, deadzonePage.triggerRange))
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.topMargin: Tokens.s3
                        implicitHeight: 1
                        color: Theme.outline
                    }

                    Kit.LabeledSwitch {
                        Layout.fillWidth: true
                        visible: deviceCard.modelData.hasGyro === true
                        label: qsTr("Forward motion (gyro)")
                        description: qsTr("Send gyroscope and accelerometer readings to the host.")
                        checked: deviceCard.modelData.forwardMotion === true
                        onToggled: App.setMotionEnabled(deviceCard.modelData.id, checked)
                    }
                    Label {
                        visible: deviceCard.modelData.hasGyro !== true
                        text: qsTr("Motion forwarding unavailable — this device has no gyroscope.")
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        topPadding: Tokens.s2
                        bottomPadding: Tokens.s2
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Label {
            text: qsTr("Overrides are stored per device and pushed into the live input processor immediately.")
            color: Theme.muted
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
