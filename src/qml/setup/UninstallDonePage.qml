// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Uninstall step 3: the sky at rest. When the settings were kept, the page
// names exactly where they stayed — what is left behind is a documented
// choice, never a surprise. The helper finishes the uninstaller's own
// cleanup after this window closes; nothing here needs to mention it again.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Close")
    readonly property bool canAdvance: true

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function primaryActivated() {
        Setup.quitSetup();
        return false;
    }

    ColumnLayout {
        id: pageColumn
        anchors.fill: parent
        spacing: Tokens.s6

        ColumnLayout {
            spacing: Tokens.s1
            Layout.fillWidth: true

            Label {
                id: heading
                text: qsTr("Removed")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Dish is off this PC.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Kit.Callout {
            visible: !Setup.wantPurgeUserData
            tone: Kit.Callout.Info
            // The location is data, not prose: the literal shell path the
            // settings actually live under, locale-independent.
            text: qsTr("Your settings stayed at %1 — delete that folder too if you want nothing left.").arg("%LOCALAPPDATA%\\Dish")
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
