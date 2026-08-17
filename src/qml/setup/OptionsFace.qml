// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// One face absorbs three pages: scope, folder, shortcuts and language.
// Reached only on a fresh install (updates lock scope and folder to the
// recorded manifest and lose the link here), so nothing on it needs an
// upgrade lock. Every verdict comes typed from the engine (dirStatus /
// diskOk); this face renders one sentence for it — highest concern first,
// un-boxed — and the verb's enabled state always agrees with the colour.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: face

    readonly property Item verbButton: verb
    // The host's Esc shortcut stands down while the language menu is open.
    readonly property bool popupOpen: langCombo.menuOpen

    signal installRequested()
    signal backRequested()

    readonly property bool invalidDir: Setup.dirStatus === Setup.DirNotAbsolute
                                       || Setup.dirStatus === Setup.DirDenied
                                       || Setup.dirStatus === Setup.DirInvalid
                                       || Setup.dirStatus === Setup.DirIsSystem
    // A folder that merely HAS files in it is advice, not an error: the
    // verdict line says exactly what continuing does, which is the informed
    // consent silent mode cannot ask for.
    readonly property bool dirAcceptable: Setup.dirStatus === Setup.DirOk
                                          || Setup.dirStatus === Setup.DirIsExistingInstall
                                          || Setup.dirStatus === Setup.DirNotEmpty
    readonly property bool oneDrivePath: Setup.installDir.toLowerCase().indexOf("onedrive") >= 0

    // At most one verdict sentence, highest concern first (the calloutKind
    // ladder, un-boxed). Low disk keeps its own always-present line below.
    readonly property int verdictKind: face.invalidDir ? 1
                                     : Setup.dirStatus === Setup.DirNotEmpty ? 2
                                     : face.oneDrivePath ? 3
                                     : 0

    Accessible.name: heading.text

    function chooseScope(scope) {
        if (Setup.scope === scope)
            return;
        Setup.scope = scope;
        // Switching scope rewrites the path to that scope's default; a custom
        // path belongs to the scope it was picked under.
        Setup.installDir = Setup.defaultDirFor(scope);
    }

    // Invokable results are not bindings, so the list is built by hand; the
    // "system" entry's label is a sentence (qsTr'd here, never vended from
    // C++), so the list follows every language change.
    property var langCodes: []
    property var langNames: []

    function rebuildLanguages() {
        const codes = Setup.availableLanguages;
        const names = [];
        for (let i = 0; i < codes.length; ++i) {
            names.push(codes[i] === "system" ? qsTr("Same as Windows")
                                             : Setup.languageDisplayName(codes[i]));
        }
        face.langCodes = codes;
        face.langNames = names;
    }

    Component.onCompleted: face.rebuildLanguages()

    Connections {
        target: Setup
        function onLanguageChanged() { face.rebuildLanguages(); }
    }

    // The key column: mono micro-labels aligned into one gutter.
    component KeyCell: Kit.Eyebrow {
        mutedTone: true
        Layout.preferredWidth: 74
        Layout.alignment: Qt.AlignVCenter
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: Tokens.s5

        Label {
            id: heading
            text: qsTr("Options")
            color: Theme.onSurface
            font.pixelSize: Tokens.textStatus
            font.bold: true
            Accessible.role: Accessible.Heading
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Kit.RadioTile {
                title: qsTr("Just me")
                body: qsTr("No admin prompt.")
                selected: Setup.scope === Setup.PerUser
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: face.chooseScope(Setup.PerUser)
            }
            Kit.RadioTile {
                title: qsTr("Everyone")
                body: qsTr("Admin approval.")
                selected: Setup.scope === Setup.AllUsers
                Layout.fillWidth: true
                Layout.preferredWidth: 1
                onClicked: face.chooseScope(Setup.AllUsers)
            }
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.fillWidth: true

            KeyCell { text: qsTr("Folder") }
            Text {
                text: Setup.installDir
                color: Theme.onSurface
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textMeta
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Kit.LinkButton {
                text: qsTr("Browse…")
                onClicked: {
                    const dir = Setup.browseForFolder();
                    if (dir.length > 0)
                        Setup.installDir = dir;
                }
            }
        }

        RowLayout {
            visible: face.verdictKind > 0
            spacing: Tokens.s4
            Layout.fillWidth: true

            Item { Layout.preferredWidth: 74 }
            Label {
                text: face.verdictKind === 1
                      ? qsTr("That folder can’t be used — check the path.")
                      : face.verdictKind === 2
                        ? qsTr("That folder already has files in it. Files with matching names will be replaced.")
                        : qsTr("That folder syncs with OneDrive. Dish runs fine there, but syncing can slow installs and updates.")
                color: face.verdictKind === 1 ? Theme.error : Theme.warning
                font.pixelSize: Tokens.textMeta
                lineHeight: 1.4
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.fillWidth: true

            Item { Layout.preferredWidth: 74 }
            Text {
                // Colour never carries this alone: when the disk verdict goes
                // red, the verb below disables with it.
                text: qsTr("Needs %1 · %2 free").arg(Setup.requiredText).arg(Setup.freeText)
                color: Setup.diskOk ? Theme.mutedStrong : Theme.error
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
                Accessible.role: Accessible.StaticText
                Accessible.name: text
                Layout.fillWidth: true
            }
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.fillWidth: true

            KeyCell { text: qsTr("Shortcuts") }
            Kit.CheckRow {
                text: qsTr("Start Menu")
                checked: Setup.wantStartMenu
                onToggled: Setup.wantStartMenu = checked
            }
            Kit.CheckRow {
                text: qsTr("Desktop")
                checked: Setup.wantDesktop
                onToggled: Setup.wantDesktop = checked
                Layout.leftMargin: Tokens.s7
            }
            Item { Layout.fillWidth: true }
        }

        RowLayout {
            spacing: Tokens.s4
            Layout.fillWidth: true

            KeyCell { text: qsTr("Language") }
            Kit.ComboButton {
                id: langCombo
                options: face.langNames
                value: {
                    const i = face.langCodes.indexOf(Setup.uiLanguage);
                    return i >= 0 && i < face.langNames.length ? face.langNames[i] : "";
                }
                onPicked: function (option) {
                    const i = face.langNames.indexOf(option);
                    if (i >= 0)
                        Setup.uiLanguage = face.langCodes[i];
                }
            }
            Item { Layout.fillWidth: true }
        }

        Item { Layout.fillHeight: true }

        Kit.DishButton {
            id: verb
            size: Kit.DishButton.Large
            variant: Kit.DishButton.Primary
            text: qsTr("Install Dish")
            enabled: face.dirAcceptable && Setup.diskOk
            Layout.fillWidth: true
            onClicked: face.installRequested()
        }

        RowLayout {
            spacing: Tokens.s8
            Layout.alignment: Qt.AlignHCenter

            Kit.LinkButton {
                text: qsTr("‹ Back")
                onClicked: face.backRequested()
            }
            Text {
                text: "·"
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
            }
            Text {
                text: qsTr("Nothing is copied yet.")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
            }
        }
    }
}
