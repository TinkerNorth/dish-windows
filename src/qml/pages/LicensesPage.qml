// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Open-source licenses detail sub-page, pushed from Settings ▸ About (SCR §7.3).
// One Kit.RowButton per bundled library — a real focusable, keyboard-activatable
// control rather than the cursor-pointer card the mock drew (D46 / SCR §12.24c);
// the version, license and url ride the row's sub-line so the whole entry reads
// in one pass. The footer binds App.appVersion (D44) — never a literal.
//
// Bound to the real `App` surface: App.licenses() rows (the bundled manifest via
// loadBundledLicenseManifest) and App.openExternalUrl (routes through
// ExternalLink so a failure raises the shared toast). docs/QML_CONTRACT.md §1b.

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
    readonly property string headerSub: qsTr("Acknowledgements for the libraries Dish is built on.")

    // The bundled manifest, mapped to { name, version, license, url } rows. Read
    // once on load (the manifest is a static resource — it never moves at runtime).
    readonly property var entries: App.licenses()

    // The design's readable measure for a long list of rows (SCR §7.3).
    readonly property int bodyWidth: 640

    // version · license · url on one sub-line. `·` is the app's one separator
    // outside the contract-defined "<ip> • UDP <port>" composition (D43).
    function metaLine(entry) {
        let parts = [];
        if (entry.version && entry.version.length > 0) { parts.push(entry.version); }
        if (entry.license && entry.license.length > 0) { parts.push(entry.license); }
        if (entry.url && entry.url.length > 0) { parts.push(entry.url); }
        return parts.join(" · ");
    }

    // Route through App.openExternalUrl so a failed open raises the same toast
    // every other external link does, not a silent Qt.openUrlExternally.
    function openUrl(url) {
        if (url && url.length > 0) {
            App.openExternalUrl(url);
        }
    }

    ColumnLayout {
        width: Math.min(licensesPage.bodyWidth, parent ? parent.width : 0)
        spacing: Tokens.s5

        // A static manifest is never empty in practice, but every list carries an
        // empty state (SYS §7.5) rather than rendering as a blank page.
        Kit.EmptyState {
            visible: licensesPage.entries.length === 0
            Layout.fillWidth: true
            glyph: "dish-off"
            title: qsTr("No license manifest")
            body: qsTr("The bundled acknowledgements could not be read from this build.")
        }

        Repeater {
            model: licensesPage.entries

            delegate: Kit.RowButton {
                id: licenseRow
                required property var modelData

                Layout.fillWidth: true
                title: licenseRow.modelData.name
                subtitle: licensesPage.metaLine(licenseRow.modelData)
                onClicked: licensesPage.openUrl(licenseRow.modelData.url)
            }
        }

        Label {
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s3
            text: qsTr("Dish %1 · TinkerNorth · LGPL-3.0").arg(App.appVersion)
            color: Theme.muted
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textMeta
            wrapMode: Text.WordWrap
        }
    }
}
