// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The uninstall confirm: the recorded install stated back as one mono facts
// line, the purge choice (defaults OFF so a reinstall restores the user),
// and the one destructive commit in the family as the verb. What stays
// behind is named honestly on the Removed face, where it is actionable.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property Item verbButton: verb

    signal removeRequested()

    readonly property string versionShown: Setup.existingVersion.length > 0
                                           ? Setup.existingVersion : Setup.appVersion

    // Pre-seeded by the engine from the recorded install choices. A fact the
    // record can't answer drops out rather than leaving an empty slot.
    readonly property string factsLine: {
        const parts = [face.shortDir(Setup.existingDir)];
        if (Setup.requiredText.length > 0)
            parts.push(Setup.requiredText);
        const places = [];
        if (Setup.wantStartMenu)
            places.push(qsTr("Start Menu"));
        if (Setup.wantDesktop)
            places.push(qsTr("Desktop"));
        if (places.length > 0)
            parts.push(places.join(" · "));
        return parts.join(" · ");
    }

    Accessible.name: heading.text + " — " + face.factsLine

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
                    width: 56
                    height: 56
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: width
                    Layout.preferredHeight: height
                }
                Label {
                    id: heading
                    text: qsTr("Remove Dish %1?").arg(face.versionShown)
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textTitle
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }
                Text {
                    text: face.factsLine
                    color: Theme.mutedStrong
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textChip
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Kit.CheckRow {
                    text: qsTr("Also remove my settings and pairings")
                    checked: Setup.wantPurgeUserData
                    onToggled: Setup.wantPurgeUserData = checked
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: Tokens.s3
                }
            }
        }

        Kit.DishButton {
            id: verb
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Destructive
            text: qsTr("Remove Dish")
            Layout.fillWidth: true
            onClicked: face.removeRequested()
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                text: qsTr("Cancel")
                onClicked: Setup.quitSetup()
            }
            Text {
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Text {
                text: qsTr("Nothing is removed yet.")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
            }
        }
    }
}
