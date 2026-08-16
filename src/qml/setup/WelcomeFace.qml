// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The welcome face, three ways: fresh install (verb + a defaults line the
// Options face edits), in-place update (scope and folder come from the
// recorded manifest, so the Options link is gone and the location line states
// them instead), and downgrade (destructive dress on the verb, the safe exit
// a first-class link — and the verb still routes through the confirm face
// before anything runs).

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property bool update: Setup.existingDetected && !Setup.isDowngrade
    readonly property bool downgrade: Setup.existingDetected && Setup.isDowngrade

    readonly property Item verbButton: verb

    signal installRequested()
    signal downgradeRequested()
    signal optionsRequested()

    Accessible.name: heading.text + " — " + sentence.text

    function shortDir(path) {
        if (!path || path.length === 0)
            return "";
        const parts = path.split(/[\\/]+/).filter(function (s) { return s.length > 0; });
        if (parts.length <= 2)
            return path;
        return "…\\" + parts[parts.length - 2] + "\\" + parts[parts.length - 1];
    }

    // The one line that answers "install with what?" before Options is ever
    // opened. Facts joined as data, not composed prose.
    readonly property string defaultsLine: {
        const scope = Setup.scope === Setup.AllUsers ? qsTr("Everyone on this PC")
                                                     : qsTr("Just me");
        const places = [];
        if (Setup.wantStartMenu)
            places.push(qsTr("Start Menu"));
        if (Setup.wantDesktop)
            places.push(qsTr("Desktop"));
        const shortcuts = places.length > 0 ? places.join(" · ") : qsTr("no shortcuts");
        return scope + " · " + face.shortDir(Setup.installDir) + " · " + shortcuts;
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
                spacing: Tokens.s6

                Kit.AppMark {
                    width: 72
                    height: 72
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: width
                    Layout.preferredHeight: height
                }

                Label {
                    id: heading
                    // The brand alone on a fresh install; the version joins it
                    // once an existing install makes versions the story.
                    text: Setup.existingDetected ? qsTr("Dish %1").arg(Setup.appVersion) : "Dish"
                    color: Theme.onSurface
                    font.pixelSize: Tokens.textDisplay
                    font.bold: true
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Accessible.role: Accessible.Heading
                    Layout.fillWidth: true
                }

                Label {
                    id: sentence
                    text: face.downgrade
                          ? qsTr("This PC has Dish %1 — newer than this installer.").arg(Setup.existingVersion)
                          : face.update
                            ? qsTr("Dish %1 is on this PC. This updates it in place — your settings and pairings stay.").arg(Setup.existingVersion)
                            : qsTr("Your controller, on a Satellite host over your LAN.")
                    color: face.downgrade ? Theme.warning : Theme.muted
                    font.pixelSize: Tokens.textSummary
                    lineHeight: 1.5
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }

                Text {
                    visible: !Setup.existingDetected
                    text: face.defaultsLine
                    color: Theme.mutedStrong
                    font.pixelSize: Tokens.textMeta
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
            }
        }

        Kit.DishButton {
            id: verb
            size: Kit.DishButton.Large
            variant: face.downgrade ? Kit.DishButton.Destructive : Kit.DishButton.Primary
            text: face.downgrade ? qsTr("Replace with %1").arg(Setup.appVersion)
                : face.update ? qsTr("Update Dish")
                : qsTr("Install Dish")
            Layout.fillWidth: true
            onClicked: {
                if (face.downgrade)
                    face.downgradeRequested();
                else
                    face.installRequested();
            }
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                visible: !Setup.existingDetected
                text: qsTr("Options")
                onClicked: face.optionsRequested()
            }
            Text {
                visible: !Setup.existingDetected
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Text {
                visible: !face.downgrade
                // Fresh: version and cost. Update: where and how much. The
                // size drops out while the disk probe has no payload answer,
                // instead of leaving a dangling separator.
                text: {
                    const parts = [face.update ? face.shortDir(Setup.existingDir)
                                               : Setup.appVersion];
                    if (Setup.requiredText.length > 0)
                        parts.push(Setup.requiredText);
                    return parts.join(" · ");
                }
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
                elide: Text.ElideMiddle
            }
            Kit.LinkButton {
                visible: face.downgrade
                text: qsTr("Keep %1 — close").arg(Setup.existingVersion)
                onClicked: Setup.quitSetup()
            }
        }
    }
}
