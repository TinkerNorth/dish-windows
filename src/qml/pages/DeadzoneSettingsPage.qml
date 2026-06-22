// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Deadzones detail sub-page (pushed from SettingsPage). Mirrors the Widgets
// DeadzoneSettingsView: a CONTROLLERS section of per-device cards, each with a
// stick and a trigger dead-zone slider and (for gyro pads) a Forward-motion
// toggle, plus the explanatory footnote.
//
// Bound to the real `App` surface: App.deadzoneDevices() rows (re-pulled on
// App.deadzonesChanged), App.setDeadzones(deviceId,stickFlat,triggerFlat), and
// App.setMotionEnabled(deviceId,bool) — forwards to the already-tested
// DeadzoneRepository + MotionEnabledStore (docs/QML_CONTRACT.md §1b).

// Bind outer-component ids (deadzonePage) into the device-card Repeater delegate.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: deadzonePage
    title: qsTr("Dead zones")

    // Slider bounds mirror DeadzoneSettingsView's kStickSliderMax / kTriggerSliderMax.
    readonly property int stickSliderMax: 10000
    readonly property int triggerSliderMax: 255

    // Device rows from App.deadzoneDevices(): { id, name, hasGyro, stickFlat,
    // triggerFlat, forwardMotion }. Re-pulled whenever the rows move (a device
    // attached/detached, or a set landed) via App.deadzonesChanged.
    property var deviceModel: App.deadzoneDevices()

    Connections {
        target: App
        function onDeadzonesChanged() { deadzonePage.deviceModel = App.deadzoneDevices(); }
    }

    Kit.SectionHeader { label: qsTr("Controllers") }

    // Empty-state copy (verbatim from DeadzoneSettingsView).
    Label {
        visible: deadzonePage.deviceModel.length === 0
        text: qsTr("Connect a controller to adjust its dead zones.")
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        width: parent ? parent.width : implicitWidth
    }

    Column {
        spacing: 10
        width: parent ? parent.width : implicitWidth
        visible: deadzonePage.deviceModel.length > 0

        Repeater {
            model: deadzonePage.deviceModel
            delegate: Kit.Card {
                id: deviceCard
                required property var modelData
                width: parent ? parent.width : implicitWidth

                contentItem: ColumnLayout {
                    spacing: 8

                    Label {
                        text: deviceCard.modelData.name.length > 0 ? deviceCard.modelData.name
                                                                   : deviceCard.modelData.id
                        color: Theme.onSurface
                        font.pixelSize: 13
                        font.bold: true
                    }

                    // Stick dead-zone slider + live value (mirror stickRow). A user
                    // move persists BOTH axes (App.setDeadzones takes the pair) and
                    // re-tunes the live processor. onMoved (not value-changed) so a
                    // binding refresh after a re-pull never re-fires the setter.
                    RowLayout {
                        spacing: 12
                        Slider {
                            id: stickSlider
                            Layout.fillWidth: true
                            from: 0
                            to: deadzonePage.stickSliderMax
                            value: deviceCard.modelData.stickFlat
                            onMoved: App.setDeadzones(deviceCard.modelData.id,
                                                      Math.round(stickSlider.value),
                                                      Math.round(triggerSlider.value))
                        }
                        Label {
                            text: qsTr("Stick: %1").arg(Math.round(stickSlider.value))
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    // Trigger dead-zone slider + live value (mirror triggerRow).
                    RowLayout {
                        spacing: 12
                        Slider {
                            id: triggerSlider
                            Layout.fillWidth: true
                            from: 0
                            to: deadzonePage.triggerSliderMax
                            value: deviceCard.modelData.triggerFlat
                            onMoved: App.setDeadzones(deviceCard.modelData.id,
                                                      Math.round(stickSlider.value),
                                                      Math.round(triggerSlider.value))
                        }
                        Label {
                            text: qsTr("Trigger: %1").arg(Math.round(triggerSlider.value))
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }

                    // Motion-forwarding toggle — only for a pad that has a gyro.
                    Kit.LabeledSwitch {
                        Layout.fillWidth: true
                        visible: deviceCard.modelData.hasGyro === true
                        label: qsTr("Forward motion")
                        checked: deviceCard.modelData.forwardMotion === true
                        onToggled: App.setMotionEnabled(deviceCard.modelData.id, checked)
                    }
                }
            }
        }
    }

    // Footnote (verbatim from DeadzoneSettingsView).
    Label {
        text: qsTr("A larger dead zone ignores more stick or trigger travel near rest — "
                 + "raise it if a worn controller drifts on its own.")
        color: Theme.muted
        font.pixelSize: 11
        wrapMode: Text.WordWrap
        width: parent ? parent.width : implicitWidth
    }
}
