// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 0: what this is, what it costs, and the honesty promise — no questions
// asked. The facts card mirrors the wizard's card language; the closing
// callout makes the promise the installer keeps: steps 0-3 write nothing to
// disk or registry. The language selector re-installs the translator live via
// Setup.uiLanguage.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Continue ›")
    readonly property bool canAdvance: true

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    // Invokable results are not bindings, so the list is built by hand. The
    // real locales are endonyms and never re-spell, but "system" is the one
    // entry whose label is a sentence — the engine returns an empty string for
    // it on purpose (sentences are qsTr'd here, never vended from C++) — so the
    // list is rebuilt whenever the language changes and that sentence follows.
    property var langCodes: []
    property var langNames: []

    function rebuildLanguages() {
        const codes = Setup.availableLanguages;
        const names = [];
        for (let i = 0; i < codes.length; ++i) {
            names.push(codes[i] === "system" ? qsTr("Same as Windows")
                                             : Setup.languageDisplayName(codes[i]));
        }
        page.langCodes = codes;
        page.langNames = names;
    }

    Component.onCompleted: page.rebuildLanguages()

    Connections {
        target: Setup
        function onLanguageChanged() { page.rebuildLanguages(); }
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
        // Fills the rest of the row and right-aligns inside it: no spacer, and
        // no width cap reading the width the layout is still deciding (that one
        // made Qt report a recursive rearrange wherever a value was empty).
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
                text: qsTr("Dish %1").arg(Setup.appVersion)
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Streams a controller plugged into this PC to a Satellite host on your network. This sets up the Windows app.")
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
                    label: qsTr("Version")
                    value: Setup.appVersion
                    mono: true
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Publisher")
                    value: "TinkerNorth" // brand, never translated
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("License")
                    value: qsTr("LGPL-3.0-or-later · free software")
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Needs")
                    value: qsTr("%1 on disk").arg(Setup.requiredText)
                    mono: true
                    Layout.fillWidth: true
                }
            }
        }

        ColumnLayout {
            spacing: Tokens.s3
            Layout.fillWidth: true

            Kit.Eyebrow {
                text: qsTr("Language")
                mutedTone: true
            }
            Kit.ComboButton {
                options: page.langNames
                value: {
                    const i = page.langCodes.indexOf(Setup.uiLanguage);
                    return i >= 0 && i < page.langNames.length ? page.langNames[i] : "";
                }
                onPicked: function (option) {
                    const i = page.langNames.indexOf(option);
                    if (i >= 0)
                        Setup.uiLanguage = page.langCodes[i];
                }
            }
        }

        Kit.Callout {
            visible: Setup.existingDetected && !Setup.isDowngrade
            tone: Kit.Callout.Info
            text: qsTr("Dish %1 is already installed here. Continuing upgrades it in place — your settings and pairings stay.").arg(Setup.existingVersion)
            Layout.fillWidth: true
        }
        Kit.Callout {
            visible: Setup.existingDetected && Setup.isDowngrade
            tone: Kit.Callout.Warning
            text: qsTr("This PC has Dish %1, newer than this installer (%2). Continuing replaces it with the older version.").arg(Setup.existingVersion).arg(Setup.appVersion)
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }

        Kit.Callout {
            tone: Kit.Callout.Info
            text: qsTr("Nothing is copied until the last step. Cancel before that and this PC is left exactly as it was.")
            Layout.fillWidth: true
        }
    }
}
