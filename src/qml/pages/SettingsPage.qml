// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Settings destination (design FSettingsBody, frame f-e1): a responsive
// two-column grid of sections. LEFT: Setup & help (setup-guide dialog + Help &
// FAQ detail), Appearance (theme segmented control), Forwarded features (light
// bar combo + footnote). RIGHT: Controller tuning (deadzones detail entry),
// Diagnostics (crash reporting), About (licenses / donate entries + the mono
// version line). The page renders no local title — the shell header shows
// `headerTitle`.
//
// Bound to the real `App` surface: App.themeMode / App.setThemeMode,
// App.lightbarFollowGame / App.setLightbarFollowGame, App.crashReportingEnabled
// / App.setCrashReportingEnabled, App.appVersion (docs/QML_CONTRACT.md §1b/§7).

// Bind outer-component ids (settingsPage) into nested handlers/delegates.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import "../onboarding"
import Dish.Chrome

Kit.Page {
    id: settingsPage
    title: qsTr("Settings")

    // Shell header contract (AppShell): title only, no sub line (frame f-e1).
    readonly property string headerTitle: qsTr("Settings")

    // Chip order == App.themeMode values (0=Light 1=Dark 2=System, §1b).
    readonly property var themeOptions: [qsTr("Light"), qsTr("Dark"), qsTr("System")]
    // Light-bar combo options (§7.1: true = "Follow game", false = "Off").
    readonly property string lightbarOn: qsTr("Follow game")
    readonly property string lightbarOff: qsTr("Off")

    // The shell facade vended by the content StackView (AppShell `shellApi`).
    // Held var-typed so the dynamic property resolves at runtime.
    readonly property var shellView: StackView.view

    // Push a detail sub-page. The URL is resolved HERE (against this page's
    // location) because the shell's own Qt.resolvedUrl resolves against
    // AppShell.qml — which would miss ../onboarding/. shellApi.pushDetail also
    // records the breadcrumb fallback title; a bare push is the fallback.
    function pushDetail(file, title) {
        if (!settingsPage.shellView) {
            return;
        }
        if (settingsPage.shellView.shellApi) {
            settingsPage.shellView.shellApi.pushDetail(Qt.resolvedUrl(file), title);
        } else {
            settingsPage.shellView.push(Qt.resolvedUrl(file));
        }
    }

    // The 3-step setup guide as an in-window dialog (design f-a2). A local
    // instance; the Popup reparents to the shell overlay on open().
    SetupGuideDialog { id: setupGuide }

    GridLayout {
        width: parent ? parent.width : implicitWidth
        // The design's two-column settings grid collapses to one column when
        // the content pane is narrow (rail expanded on a small window).
        columns: settingsPage.width < 760 ? 1 : 2
        columnSpacing: Tokens.pagePadding
        rowSpacing: Tokens.pagePadding

        // ── LEFT column ──────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s8

            // Setup & help
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Setup & help") }

                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Setup guide")
                    subtitle: qsTr("Walk through connection and controller setup. Re-run any time.")
                    onClicked: setupGuide.open()
                }
                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Help & FAQ")
                    subtitle: qsTr("Concepts, performance tips, and troubleshooting.")
                    onClicked: settingsPage.pushDetail("../onboarding/HelpScreen.qml",
                                                       qsTr("Help & FAQ"))
                }
            }

            // Appearance
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Appearance") }

                Kit.Card {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        spacing: 0

                        Label {
                            text: qsTr("Theme")
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                        }
                        Label {
                            text: qsTr("Choose how Dish looks. System matches your Windows light or dark setting.")
                            color: Theme.muted
                            font.pixelSize: Tokens.textMeta
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.topMargin: Tokens.s1
                        }
                        Kit.SegmentedControl {
                            Layout.topMargin: Tokens.s5
                            options: settingsPage.themeOptions
                            value: settingsPage.themeOptions[App.themeMode]
                            // setThemeMode persists + republishes (ThemePreferenceStore)
                            // and re-themes the live app + native chrome (§1b).
                            onPicked: (option) => App.setThemeMode(settingsPage.themeOptions.indexOf(option))
                        }
                    }
                }
            }

            // Forwarded features
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Forwarded features") }

                Kit.Card {
                    Layout.fillWidth: true
                    contentItem: RowLayout {
                        spacing: Tokens.s6

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: Tokens.s1

                            Label {
                                text: qsTr("Light bar")
                                color: Theme.onSurface
                                font.pixelSize: Tokens.textBase
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: qsTr("Follow game: the controller LED matches the host game. Off: leave the LED untouched.")
                                color: Theme.muted
                                font.pixelSize: Tokens.textMeta
                                wrapMode: Text.WordWrap
                                Layout.fillWidth: true
                            }
                        }

                        Kit.ComboButton {
                            Layout.alignment: Qt.AlignVCenter
                            options: [settingsPage.lightbarOn, settingsPage.lightbarOff]
                            value: App.lightbarFollowGame ? settingsPage.lightbarOn
                                                          : settingsPage.lightbarOff
                            onPicked: (option) => App.setLightbarFollowGame(option === settingsPage.lightbarOn)
                        }
                    }
                }

                Label {
                    text: qsTr("Features only apply when your controller's hardware supports them — the controller list shows what was detected.")
                    color: Theme.muted
                    font.pixelSize: Tokens.textMeta
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // ── RIGHT column ─────────────────────────────────────────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s8

            // Controller tuning
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Controller tuning") }

                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Dead zones & motion…")
                    subtitle: qsTr("Per-device stick / trigger flats and gyro forwarding.")
                    onClicked: settingsPage.pushDetail("DeadzoneSettingsPage.qml",
                                                       qsTr("Dead zones & motion"))
                }
            }

            // Diagnostics
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Diagnostics") }

                Kit.Card {
                    Layout.fillWidth: true
                    contentItem: Kit.LabeledSwitch {
                        label: qsTr("Crash reporting")
                        description: qsTr("Anonymous crash reports help fix bugs. Opt out any time.")
                        checked: App.crashReportingEnabled
                        // Forwards to CrashReportingStore::setEnabled (opt-out, default on).
                        onToggled: (checked) => App.setCrashReportingEnabled(checked)
                    }
                }
            }

            // About
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("About") }

                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Open source licenses")
                    subtitle: qsTr("Acknowledgements for the libraries Dish is built on.")
                    onClicked: settingsPage.pushDetail("LicensesPage.qml",
                                                       qsTr("Open source licenses"))
                }
                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Support Dish")
                    subtitle: qsTr("Donate via GitHub Sponsors, Ko-fi, or Buy Me a Coffee.")
                    onClicked: settingsPage.pushDetail("DonatePage.qml", qsTr("Support Dish"))
                }

                Label {
                    text: qsTr("Dish %1 · TinkerNorth · LGPL-3.0").arg(App.appVersion)
                    color: Theme.muted
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textMeta
                    Layout.topMargin: Tokens.s1
                }
            }
        }
    }
}
