// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Uninstall step 1: the summary and the one destructive commit in the family
// (the footer's Remove runs as the Destructive variant, the Unbind
// precedent). The purge switch defaults OFF so a reinstall restores the
// user; what stays behind is named honestly on the Removed page. Nothing is
// removed until Remove — and the footer hint says exactly that.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    // Kept short on purpose: the footer composes "<position> · <hint>" into one
    // elided line, and the longer form was cut mid-word ("Nothing is removed
    // un…"). This says the same thing and mirrors the install side's own
    // "Nothing is copied yet.", which fits in the same space.
    readonly property string hint: qsTr("Nothing is removed yet.")
    readonly property string primaryLabel: qsTr("Remove")
    readonly property bool canAdvance: true

    signal removeRequested()

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function primaryActivated() {
        page.removeRequested();
        return false; // the phase change advances the step, not the footer
    }

    readonly property string versionShown: Setup.existingVersion.length > 0
                                           ? Setup.existingVersion : Setup.appVersion

    // Pre-seeded by the engine from the recorded install choices.
    readonly property string shortcutsValue: {
        const places = [];
        if (Setup.wantStartMenu)
            places.push(qsTr("Start Menu"));
        if (Setup.wantDesktop)
            places.push(qsTr("Desktop"));
        return places.length > 0 ? places.join(" · ") : qsTr("none");
    }

    component FactRow: RowLayout {
        id: factRow

        property string label: ""
        property string value: ""
        property bool mono: false

        spacing: Tokens.s6

        Accessible.role: Accessible.StaticText
        Accessible.name: factRow.label + " — " + factRow.value

        Kit.Eyebrow {
            text: factRow.label
            mutedTone: true
            Layout.alignment: Qt.AlignVCenter
        }
        // The value fills the rest of the row and right-aligns inside it, so it
        // needs no spacer and no width cap. The cap it replaces was
        // `factRow.width / 2`, an attached Layout property reading the width the
        // layout was still deciding — Qt answered "Detected recursive rearrange"
        // on this page and gave up mid-pass.
        Text {
            text: factRow.value
            color: Theme.onSurface
            font.pixelSize: Tokens.textSummary
            font.family: factRow.mono ? Tokens.monoFamily : Tokens.sansFamily
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
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
                text: qsTr("Remove Dish?")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Takes Dish %1 off this PC.").arg(page.versionShown)
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Kit.Card {
            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Tokens.s3

                FactRow {
                    label: qsTr("Folder")
                    value: Setup.existingDir
                    mono: true
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Size")
                    value: Setup.requiredText
                    mono: true
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Shortcuts")
                    value: page.shortcutsValue
                    Layout.fillWidth: true
                }
            }
        }

        Kit.LabeledSwitch {
            label: qsTr("Also remove my settings")
            description: qsTr("Pairings and preferences stored under your user profile.")
            checked: Setup.wantPurgeUserData
            onToggled: function (checked) { Setup.wantPurgeUserData = checked; }
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        Kit.Callout {
            tone: Kit.Callout.Info
            text: qsTr("Satellite hosts remember this PC until you forget it there.")
            Layout.fillWidth: true
        }
    }
}
