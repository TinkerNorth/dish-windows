// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Deadzones detail sub-page (pushed from SettingsPage). Mirrors the Widgets
// DeadzoneSettingsView: a CONTROLLERS section of per-device cards, each with a
// stick and a trigger dead-zone slider and (for gyro pads) a Forward-motion
// toggle, plus the explanatory footnote.
//
// CONTRACT NOTE: the device list and the deadzone/motion stores are NOT on the
// frozen `App` surface. This page binds a LOCAL placeholder `deviceModel` and
// shows the empty-state by default; the coordinator wires the real repository.
// Mappings (see report):
//   deviceModel       → DeadzoneRepository device rows (test_deadzone_repository)
//   per-row stick/trigger setters → DeadzoneRepository::setDeadzones
//   per-row forwardMotion → MotionEnabledStore::setEnabled

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

    // Placeholder device rows. Shape mirrors the Widgets DeviceRow + seeded
    // Deadzones: { id, name, hasGyro, stickFlat, triggerFlat, forwardMotion }.
    // Empty by default → the empty-state renders (coordinator supplies the real
    // repository-backed model).
    property var deviceModel: []

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

                    // Stick dead-zone slider + live value (mirror stickRow).
                    RowLayout {
                        spacing: 12
                        Slider {
                            id: stickSlider
                            Layout.fillWidth: true
                            from: 0
                            to: deadzonePage.stickSliderMax
                            value: deviceCard.modelData.stickFlat
                            // repo_->setDeadzones(...) in Widgets; placeholder no-op.
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
                        // motionStore_->setEnabled(slotKey, on) in Widgets.
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
