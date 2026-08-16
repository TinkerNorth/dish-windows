// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Installed, at rest: the mark holds full opacity, the result reads from the
// heading and the facts line. The verb IS the launch — no switch-plus-Finish
// — and the quiet exit is a link. The host maps window-close to the same
// finish-without-launch.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property Item verbButton: verb

    signal finishRequested()

    Accessible.name: heading.text + " — " + factsLine.text

    function shortDir(path) {
        if (!path || path.length === 0)
            return "";
        const parts = path.split(/[\\/]+/).filter(function (s) { return s.length > 0; });
        if (parts.length <= 2)
            return path;
        return "…\\" + parts[parts.length - 2] + "\\" + parts[parts.length - 1];
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Tokens.s6

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ColumnLayout {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                spacing: Tokens.s5

                Kit.AppMark {
                    width: 64
                    height: 64
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: width
                    Layout.preferredHeight: height
                }
                Label {
                    id: heading
                    text: qsTr("Installed")
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Text {
                    id: factsLine
                    // Data, not prose: brand, version, where.
                    text: "Dish " + Setup.appVersion + " · " + face.shortDir(Setup.installDir)
                    color: Theme.mutedStrong
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        Kit.DishButton {
            id: verb
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Primary
            text: qsTr("Open Dish")
            Layout.fillWidth: true
            onClicked: Setup.finishAndLaunch()
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                text: qsTr("Finish without opening")
                onClicked: face.finishRequested()
            }
            Text {
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Text {
                text: qsTr("Remove any time from Installed apps")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
            }
        }
    }
}
