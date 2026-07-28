// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Open-source licenses detail sub-page (pushed from SettingsPage). Mirrors the
// Widgets LicensesView: an intro line then one card per bundled library showing
// its name, version (monospace) and license label, opening the license URL when
// tapped.
//
// Bound to the real `App` surface: App.licenses() rows (the bundled manifest via
// loadBundledLicenseManifest, mapped with the same display rules the Widgets
// LicensesView uses) and App.openExternalUrl (routes through ExternalLink so a
// failure raises a toast). See docs/QML_CONTRACT.md §1b.

// Bind outer-component ids (licensesPage) into the license-row Repeater delegate.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: licensesPage
    title: qsTr("Open source licenses")

    readonly property string headerTitle: qsTr("Open source licenses")
    readonly property string headerSub: qsTr("Acknowledgements for the libraries Dish is built on")

    // The bundled manifest, mapped to { name, version, license, url } rows. Read
    // once on load (the manifest is a static resource — it never moves at runtime).
    readonly property var entries: App.licenses()

    Kit.SectionHeader { label: qsTr("Open source licenses") }

    Label {
        text: qsTr("Acknowledgements for the libraries Dish is built on. Tap a row to open its license.")
        color: Theme.muted
        font.pixelSize: 12
        wrapMode: Text.WordWrap
        width: parent ? parent.width : implicitWidth
    }

    Column {
        spacing: 10
        width: parent ? parent.width : implicitWidth

        Repeater {
            model: licensesPage.entries
            delegate: Kit.Card {
                id: licenseCard
                required property var modelData
                width: parent ? parent.width : implicitWidth

                contentItem: ColumnLayout {
                    spacing: 2

                    RowLayout {
                        spacing: 8
                        Label {
                            text: licenseCard.modelData.name
                            color: Theme.onSurface
                            font.pixelSize: 13
                            font.bold: true
                            Layout.fillWidth: true
                        }
                        Label {
                            text: licenseCard.modelData.version
                            visible: licenseCard.modelData.version.length > 0
                            color: Theme.muted
                            font.pixelSize: 11
                            font.family: "Consolas"
                        }
                    }

                    Label {
                        text: licenseCard.modelData.license
                        visible: licenseCard.modelData.license.length > 0
                        color: Theme.muted
                        font.pixelSize: 11
                    }
                }

                // Whole-card click opens the license URL (mirrors ClickableCard).
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: licensesPage.openUrl(licenseCard.modelData.url)
                }
            }
        }
    }

    // Route through App.openExternalUrl so a failed open raises the same toast the
    // Widgets LicensesView does (via ExternalLink/NotificationQueue), not a raw
    // Qt.openUrlExternally that fails silently.
    function openUrl(url) {
        if (url && url.length > 0) {
            App.openExternalUrl(url);
        }
    }
}
