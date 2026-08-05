// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 2: install mode + folder — the app's two-up OptionCard question
// pattern turned onto "Where should it live?". An upgrade locks mode and
// folder to the existing install (moving = uninstall + install; the footer
// hint says so). Every validation verdict comes typed from the engine
// (dirStatus / diskOk); this page only renders the sentence for it, and the
// space line pairs its colour change with a field error so colour is never
// the only signal.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    readonly property bool upgradeLock: Setup.existingDetected
    // A folder that merely HAS files in it is advice, not an error: the warning
    // callout below tells the user exactly what continuing does ("Files with
    // matching names will be replaced"), which is the informed consent silent
    // mode cannot ask for. Blocking here would have contradicted that sentence
    // on screen, and would also have hard-blocked repairing an install whose
    // manifest went missing.
    readonly property bool dirAcceptable: Setup.dirStatus === Setup.DirOk
                                          || Setup.dirStatus === Setup.DirIsExistingInstall
                                          || Setup.dirStatus === Setup.DirNotEmpty
    readonly property bool invalidDir: Setup.dirStatus === Setup.DirNotAbsolute
                                       || Setup.dirStatus === Setup.DirDenied
                                       || Setup.dirStatus === Setup.DirInvalid
                                       || Setup.dirStatus === Setup.DirIsSystem
    readonly property bool oneDrivePath: Setup.installDir.toLowerCase().indexOf("onedrive") >= 0

    // At most one advisory callout, highest concern first.
    readonly property int calloutKind: page.upgradeLock && !Setup.isDowngrade ? 1
                                     : page.upgradeLock && Setup.isDowngrade ? 2
                                     : Setup.dirStatus === Setup.DirNotEmpty ? 3
                                     : page.oneDrivePath ? 4
                                     : 0

    // Position text only. The footer composes "<position> · <hint>" into ~225px
    // at textMeta, and "Step 2 of 3 · Location" alone eats more than half of it,
    // so any advice sentence elided to a cut-off fragment ("Changed your min…").
    // The visual spec's own rule for this page: keep the hint only if it fits,
    // else position only. Nothing is lost — the callouts carry the advice.
    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Continue ›")
    readonly property bool canAdvance: page.dirAcceptable && Setup.diskOk

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function focusFirst() {
        if (Setup.scope === Setup.AllUsers)
            allUsersCard.forceActiveFocus(Qt.TabFocusReason);
        else
            perUserCard.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    function chooseScope(scope) {
        if (Setup.scope === scope)
            return;
        Setup.scope = scope;
        // Switching mode rewrites the path to that mode's default; a custom
        // path belongs to the mode it was typed under.
        Setup.installDir = Setup.defaultDirFor(scope);
    }

    // The field breaks its binding on first keystroke, so engine-side rewrites
    // (scope switch, upgrade lock) are pushed back in by hand.
    Connections {
        target: Setup
        function onOptionsChanged() {
            if (dirField.text !== Setup.installDir)
                dirField.text = Setup.installDir;
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
                text: qsTr("Where should it live?")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("One folder holds the app and its runtime. Your settings live in your user profile either way.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Kit.Eyebrow {
            text: qsTr("Install for")
            mutedTone: true
        }

        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Kit.OptionCard {
                id: perUserCard
                title: qsTr("Just me")
                badgeText: qsTr("Recommended")
                badgeTone: Kit.CapabilityChip.Ok
                body: qsTr("No administrator prompt. Installs under your user folder.")
                selected: Setup.scope === Setup.PerUser
                enabled: !page.upgradeLock
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: page.chooseScope(Setup.PerUser)
            }
            Kit.OptionCard {
                id: allUsersCard
                title: qsTr("Everyone on this PC")
                body: qsTr("Asks for administrator approval and installs under Program Files.")
                selected: Setup.scope === Setup.AllUsers
                enabled: !page.upgradeLock
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: page.chooseScope(Setup.AllUsers)
            }
        }

        Kit.Eyebrow {
            text: qsTr("Folder")
            mutedTone: true
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.fillWidth: true

            Kit.KitTextField {
                id: dirField
                enabled: !page.upgradeLock
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textSummary
                hasError: page.invalidDir || !Setup.diskOk
                errorText: page.invalidDir
                    ? qsTr("That folder can’t be used — check the path.")
                    : !Setup.diskOk ? qsTr("Not enough free space on this drive.")
                                    : ""
                Layout.fillWidth: true
                onTextEdited: Setup.installDir = dirField.text
                Component.onCompleted: dirField.text = Setup.installDir
            }
            Kit.DishButton {
                text: qsTr("Browse…")
                variant: Kit.DishButton.Outline
                enabled: !page.upgradeLock
                // Pinned to the field frame so the row does not re-centre when
                // the error line reserves its height below the field.
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Tokens.s1
                onClicked: {
                    const dir = Setup.browseForFolder();
                    if (dir.length > 0)
                        Setup.installDir = dir;
                }
            }
        }

        Text {
            text: qsTr("Needs %1 · %2 free").arg(Setup.requiredText).arg(Setup.freeText)
            color: Setup.diskOk ? Theme.mutedStrong : Theme.error
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textChip
            Accessible.role: Accessible.StaticText
            Accessible.name: text
        }

        Kit.Callout {
            visible: page.calloutKind === 1
            tone: Kit.Callout.Info
            text: qsTr("Dish %1 is already installed here. Continuing upgrades it in place — your settings and pairings stay.").arg(Setup.existingVersion)
            Layout.fillWidth: true
        }
        Kit.Callout {
            visible: page.calloutKind === 2
            tone: Kit.Callout.Warning
            text: qsTr("This PC has Dish %1, newer than this installer (%2). Continuing replaces it with the older version.").arg(Setup.existingVersion).arg(Setup.appVersion)
            Layout.fillWidth: true
        }
        Kit.Callout {
            visible: page.calloutKind === 3
            tone: Kit.Callout.Warning
            text: qsTr("That folder already has files in it. Files with matching names will be replaced.")
            Layout.fillWidth: true
        }
        Kit.Callout {
            visible: page.calloutKind === 4
            tone: Kit.Callout.Warning
            text: qsTr("That folder syncs with OneDrive. Dish runs fine there, but syncing can slow installs and updates.")
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
