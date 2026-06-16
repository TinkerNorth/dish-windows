// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Settings destination — a Win11-Settings-style sectioned page mirroring the
// Widgets SettingsView. Each surface is a Kit.Card grouped under a SectionHeader:
// Appearance (theme), Deadzones (entry → detail sub-page), Diagnostics (crash
// reporting), About (version + Licenses entry), Support (Donate entry).
//
// CONTRACT NOTE: most of these settings stores are NOT yet on the frozen `App`
// surface (docs/QML_CONTRACT.md exposes slots/connections/telemetry/pairing
// only). Each data/action surface below binds to a clearly-named placeholder
// property and is flagged in this agent's report for the coordinator to wire.
// No business logic lives here — the placeholders stand in for App.* bindings.

// Bind outer-component ids into nested Repeater delegates (qmllint-clean
// qualified access to `settingsPage` from inside the theme-chip delegate).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: settingsPage
    title: qsTr("Settings")

    // ── Placeholder contract surfaces ────────────────────────────────────────
    // These mirror the Widgets stores. They are LOCAL placeholders so the page
    // renders and reads naturally; the coordinator replaces each with the real
    // App.* binding once the contract is extended (see report). Documented:
    //   themeMode        → ThemePreferenceStore (test_theme_store)
    //   crashReporting   → CrashReportingStore  (test_crash_reporting)
    //   appVersion       → app version string   (CMake project VERSION)
    property int themeMode: 2                 // 0=Light 1=Dark 2=System
    property bool crashReportingEnabled: true // CrashReportingStore default = on
    readonly property string appName: qsTr("Dish")
    readonly property string appVersion: "0.1.0"  // placeholder → App.appVersion

    // ── Appearance ───────────────────────────────────────────────────────────
    Kit.SectionHeader { label: qsTr("Appearance") }

    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 8

            Label {
                text: qsTr("Theme")
                color: Theme.onSurface
                font.pixelSize: 13
                font.bold: true
            }
            Label {
                text: qsTr("Choose how Dish looks. System matches your Windows light or dark setting.")
                color: Theme.muted
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }

            // Three-way segmented control (Light / Dark / System). Exclusive —
            // exactly the QButtonGroup behaviour of the Widgets theme chips.
            RowLayout {
                spacing: 6
                Repeater {
                    model: [
                        { label: qsTr("Light"), mode: 0 },
                        { label: qsTr("Dark"), mode: 1 },
                        { label: qsTr("System"), mode: 2 }
                    ]
                    delegate: Button {
                        id: chip
                        required property var modelData
                        text: modelData.label
                        checkable: true
                        checked: settingsPage.themeMode === modelData.mode
                        font.pixelSize: 12
                        implicitHeight: 30
                        leftPadding: 14
                        rightPadding: 14
                        // setMode persists + republishes in the store; here we
                        // only move the placeholder (coordinator wires the call).
                        onClicked: settingsPage.themeMode = modelData.mode

                        contentItem: Text {
                            text: chip.text
                            font: chip.font
                            color: chip.checked ? Theme.background : Theme.onSurface
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            radius: 8
                            color: chip.checked ? Theme.primary
                                 : chip.hovered ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.10)
                                 : "transparent"
                            border.width: 1
                            border.color: chip.checked ? Theme.primary : Theme.outline
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    // ── Controller tuning (Deadzones) ────────────────────────────────────────
    Kit.SectionHeader { label: qsTr("Controller tuning") }

    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: RowLayout {
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: qsTr("Dead zones & motion")
                    color: Theme.onSurface
                    font.pixelSize: 13
                    font.bold: true
                }
                Label {
                    text: qsTr("Tune per-device stick and trigger dead zones, and motion forwarding.")
                    color: Theme.muted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            Kit.OutlineButton {
                text: qsTr("Open")
                // Push the deadzone detail onto the content StackView.
                onClicked: settingsPage.openDetail("DeadzoneSettingsPage.qml")
            }
        }
    }

    // ── Diagnostics ──────────────────────────────────────────────────────────
    Kit.SectionHeader { label: qsTr("Diagnostics") }

    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: Kit.LabeledSwitch {
            width: parent ? parent.width : implicitWidth
            label: qsTr("Share crash reports")
            description: qsTr("On by default — turn off to opt out. Shares anonymized crash logs and "
                            + "stack traces to help fix bugs; no gameplay or controller input is "
                            + "included. (No crash backend is wired yet.)")
            checked: settingsPage.crashReportingEnabled
            // store->setEnabled(on) in Widgets; placeholder mirror here.
            onToggled: settingsPage.crashReportingEnabled = checked
        }
    }

    // ── About ────────────────────────────────────────────────────────────────
    Kit.SectionHeader { label: qsTr("About") }

    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 10

            RowLayout {
                spacing: 12
                Kit.BrandGlyph {
                    glyph: "dish"
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: settingsPage.appName
                        color: Theme.onSurface
                        font.pixelSize: 14
                        font.bold: true
                    }
                    Label {
                        text: qsTr("Version %1").arg(settingsPage.appVersion)
                        color: Theme.muted
                        font.pixelSize: 11
                        font.family: "Consolas"
                    }
                }
            }

            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.outline }

            RowLayout {
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label {
                        text: qsTr("Open source licenses")
                        color: Theme.onSurface
                        font.pixelSize: 13
                        font.bold: true
                    }
                    Label {
                        text: qsTr("Acknowledgements for the libraries Dish is built on.")
                        color: Theme.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
                Kit.OutlineButton {
                    text: qsTr("View")
                    onClicked: settingsPage.openDetail("LicensesPage.qml")
                }
            }
        }
    }

    // ── Support ──────────────────────────────────────────────────────────────
    Kit.SectionHeader { label: qsTr("Support") }

    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: RowLayout {
            spacing: 12

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: qsTr("Support Dish")
                    color: Theme.onSurface
                    font.pixelSize: 13
                    font.bold: true
                }
                Label {
                    text: qsTr("Donate via GitHub Sponsors, Ko-fi, or Buy Me a Coffee.")
                    color: Theme.muted
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Kit.KitButton {
                text: qsTr("Donate")
                onClicked: settingsPage.openDetail("DonatePage.qml")
            }
        }
    }

    // Push a detail sub-page onto the enclosing content StackView (the supported
    // simple pattern, QML_UI_KIT.md §3). The shell shows a Back affordance once
    // the stack has depth > 1; a bare push keeps the destination's rail label.
    function openDetail(file) {
        if (StackView.view) {
            StackView.view.push(Qt.resolvedUrl(file));
        }
    }
}
