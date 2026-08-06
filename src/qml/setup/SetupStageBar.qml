// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The marker-row half of WizardBanner, without the slot row: the hero rail
// already draws the thing being built, so this bar only answers "where am I".
// Three stage markers (PACKAGE / DESTINATION / INSTALL) plus two sub-step pips
// for the current stage. Completed markers are controls (jump back, free
// because Back is non-destructive) until `locked` — from the install step on a
// copied install cannot be un-chosen. A todo marker is information: full
// opacity, its state carried in colour, never faded.

// Bound: the marker and pip delegates read the outer `stageBar` id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit

Kit.Card {
    id: stageBar

    // 1-based current stage; markers below it read done, above it todo.
    property int stage: 1
    // 0-based sub-position inside the current stage.
    property int subStep: 0
    // How many pages the CURRENT stage holds. Stages are not all the same
    // length: PACKAGE is a single page, DESTINATION is two.
    property int subSteps: 2
    // From the install step on the completed markers stop being clickable.
    property bool locked: false

    signal stageClicked(int stage)

    filled: false
    dense: true

    function markerState(n) {
        if (n < stageBar.stage)
            return "done";
        if (n === stageBar.stage)
            return "on";
        return "todo";
    }

    // Kept out of the delegate so the drawn state cannot drift from what is
    // announced.
    function markerStateLabel(state) {
        if (state === "done")
            return qsTr("done");
        if (state === "on")
            return qsTr("current");
        return qsTr("not started");
    }

    contentItem: RowLayout {
        spacing: Tokens.s5

        Repeater {
            model: [qsTr("Package"), qsTr("Destination"), qsTr("Install")]

            delegate: AbstractButton {
                id: marker

                required property int index
                required property string modelData

                readonly property int stageNumber: marker.index + 1
                readonly property string markerState: stageBar.markerState(marker.stageNumber)
                readonly property bool completed: marker.markerState === "done"
                readonly property bool current: marker.markerState === "on"
                readonly property bool clickable: marker.completed && !stageBar.locked

                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter

                enabled: marker.clickable
                opacity: 1.0
                focusPolicy: marker.clickable ? Qt.StrongFocus : Qt.NoFocus
                hoverEnabled: marker.clickable

                Accessible.role: Accessible.Button
                Accessible.name: qsTr("Step %1, %2").arg(marker.stageNumber)
                                                    .arg(marker.modelData)
                Accessible.description: stageBar.markerStateLabel(marker.markerState)

                HoverHandler {
                    enabled: marker.clickable
                    cursorShape: Qt.PointingHandCursor
                }

                onClicked: stageBar.stageClicked(marker.stageNumber)

                background: Item {
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: -Tokens.s1
                        radius: Tokens.radiusButton
                        visible: marker.visualFocus
                        color: "transparent"
                        border.width: 2
                        border.color: Theme.focusRing
                    }
                }

                contentItem: RowLayout {
                    spacing: Tokens.s3

                    Rectangle {
                        Layout.preferredWidth: 16
                        Layout.preferredHeight: 16
                        Layout.alignment: Qt.AlignVCenter
                        radius: width / 2
                        color: marker.current ? Theme.primary
                             : marker.completed ? Theme.primaryFill
                             : "transparent"
                        border.width: marker.markerState === "todo" ? 1 : 0
                        border.color: Theme.outline

                        Behavior on color {
                            enabled: !Tokens.reducedMotion
                            ColorAnimation { duration: Tokens.durFast }
                        }

                        Text {
                            anchors.centerIn: parent
                            text: marker.completed ? "✓" : String(marker.stageNumber)
                            font.pixelSize: Tokens.textChip
                            font.weight: marker.current ? Font.Bold : Font.Normal
                            color: marker.current ? Theme.onPrimary
                                 : marker.completed ? Theme.primary
                                 : Theme.mutedStrong
                        }
                    }

                    Text {
                        text: marker.modelData
                        elide: Text.ElideRight
                        font.family: Tokens.monoFamily
                        font.pixelSize: Tokens.textChip
                        font.letterSpacing: Tokens.sectionLetterSpacing
                        // Uppercased by FONT, never in the string: an
                        // uppercased literal cannot be translated.
                        font.capitalization: Font.AllUppercase
                        color: marker.current ? Theme.primary
                             : marker.completed ? Theme.onSurface
                             : Theme.mutedStrong
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                    }
                }
            }
        }

        // The sub-step pips, one per page in the current stage. A single-page
        // stage draws a single pip rather than a permanently unfilled pair.
        Row {
            spacing: Tokens.s1
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

            Accessible.role: Accessible.StaticText
            Accessible.name: qsTr("Sub-step %1 of %2").arg(stageBar.subStep + 1)
                                                      .arg(stageBar.subSteps)

            Repeater {
                model: stageBar.subSteps

                delegate: Rectangle {
                    id: pip

                    required property int index

                    width: Tokens.s3
                    height: Tokens.s3
                    radius: width / 2
                    color: pip.index <= stageBar.subStep ? Theme.primary : "transparent"
                    border.width: 1
                    border.color: pip.index <= stageBar.subStep ? Theme.primary
                                                                : Theme.outline

                    Behavior on color {
                        enabled: !Tokens.reducedMotion
                        ColorAnimation { duration: Tokens.durFast }
                    }
                }
            }
        }
    }
}
