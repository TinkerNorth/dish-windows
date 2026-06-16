// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Open-source licenses detail sub-page (pushed from SettingsPage). Mirrors the
// Widgets LicensesView: an intro line then one card per bundled library showing
// its name, version (monospace) and license label, opening the license URL when
// tapped.
//
// CONTRACT NOTE: the license manifest is NOT on the frozen `App` surface. The
// rows below are SEEDED from assets/licenses/licenses.json (the same bundle the
// Widgets view parses) as a LOCAL placeholder so the page lists real
// attributions; the coordinator should replace `entries` with the manifest
// binding and `openUrl` with the real external-link action. Mappings (report):
//   entries → LicenseManifest / loadBundledLicenseManifest (test_license_manifest)
//   openUrl → ExternalLink openExternalUrl

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

    // Seeded from assets/licenses/licenses.json. Shape: { name, version, license, url }.
    readonly property var entries: [
        { name: "Qt 6", version: "6.7.3",
          license: qsTr("GNU Lesser General Public License v3.0"),
          url: "https://www.gnu.org/licenses/lgpl-3.0.html" },
        { name: "Simple DirectMedia Layer (SDL2)", version: "2.30",
          license: qsTr("zlib License"),
          url: "https://www.zlib.net/zlib_license.html" },
        { name: "libsodium", version: "1.0.20",
          license: qsTr("ISC License"),
          url: "https://opensource.org/license/isc-license-txt" },
        { name: "Catch2", version: "3.5.4",
          license: qsTr("Boost Software License 1.0"),
          url: "https://www.boost.org/LICENSE_1_0.txt" }
    ]

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

    // Placeholder external-open. Qt.openUrlExternally is a platform call (not
    // business logic); the coordinator may route it through the app's notifying
    // ExternalLink helper instead.
    function openUrl(url) {
        if (url && url.length > 0) {
            Qt.openUrlExternally(url);
        }
    }
}
