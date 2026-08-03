// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Settings destination: one scrolling column that reflows to two above
// Tokens.wideBreakpoint. The page renders no local title — the shell header
// shows `headerTitle`.

// Bound: nested handlers/delegates reference the outer `settingsPage` id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: settingsPage
    title: qsTr("Settings")

    readonly property string headerTitle: qsTr("Settings")

    // Order is load-bearing: an option's index IS its App.themeMode value.
    readonly property var themeOptions: [qsTr("Light"), qsTr("Dark"), qsTr("System")]
    readonly property string lightbarOn: qsTr("Follow game")
    readonly property string lightbarOff: qsTr("Off")

    // var-typed: `shellApi` is a dynamic property, resolved at runtime.
    readonly property var shellView: StackView.view
    readonly property var shellApi: settingsPage.shellView ? settingsPage.shellView.shellApi : null

    // Resolve the URL HERE: the shell's own Qt.resolvedUrl resolves against
    // AppShell.qml, which would miss ../onboarding/.
    function pushDetail(file, title) {
        if (!settingsPage.shellView) {
            return;
        }
        if (settingsPage.shellApi) {
            settingsPage.shellApi.pushDetail(Qt.resolvedUrl(file), title);
        } else {
            settingsPage.shellView.push(Qt.resolvedUrl(file));
        }
    }

    GridLayout {
        width: parent ? parent.width : implicitWidth
        columns: settingsPage.width < Tokens.wideBreakpoint ? 1 : 2
        columnSpacing: Tokens.pagePadding
        rowSpacing: Tokens.pagePadding

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Setup & help") }

                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Set up Dish")
                    subtitle: qsTr("Walk through input, destination and binding. Re-run any time.")
                    onClicked: if (settingsPage.shellApi) settingsPage.shellApi.openSetupWizard("")
                }
                Kit.RowButton {
                    Layout.fillWidth: true
                    title: qsTr("Help & FAQ")
                    subtitle: qsTr("Concepts, performance tips, and troubleshooting.")
                    onClicked: settingsPage.pushDetail("../onboarding/HelpScreen.qml",
                                                       qsTr("Help & FAQ"))
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Appearance") }

                Kit.Card {
                    Layout.fillWidth: true
                    contentItem: ColumnLayout {
                        // Flush: the rows carry their own top margins, so a
                        // layout gap would double-space the label stack.
                        spacing: Tokens.s0

                        Label {
                            text: qsTr("Theme")
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textBase
                            font.weight: Font.DemiBold
                        }
                        Label {
                            Layout.fillWidth: true
                            Layout.topMargin: Tokens.s1
                            text: qsTr("Choose how Dish looks. System matches your Windows light or dark setting.")
                            color: Theme.muted
                            font.pixelSize: Tokens.textMeta
                            wrapMode: Text.WordWrap
                        }
                        Kit.SegmentedControl {
                            Layout.topMargin: Tokens.s5
                            options: settingsPage.themeOptions
                            value: settingsPage.themeOptions[App.themeMode]
                            onPicked: (option) => App.setThemeMode(settingsPage.themeOptions.indexOf(option))
                        }
                    }
                }
            }

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
                                Layout.fillWidth: true
                                text: qsTr("Follow game: the controller LED matches the host game. Off: leave the LED untouched.")
                                color: Theme.muted
                                font.pixelSize: Tokens.textMeta
                                wrapMode: Text.WordWrap
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
                    Layout.fillWidth: true
                    text: qsTr("Features only apply when your controller’s hardware supports them — the controller list shows what was detected.")
                    color: Theme.muted
                    font.pixelSize: Tokens.textMeta
                    wrapMode: Text.WordWrap
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s8

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

            ColumnLayout {
                Layout.fillWidth: true
                spacing: Tokens.s4

                Kit.SectionHeader { label: qsTr("Diagnostics") }

                Kit.Card {
                    Layout.fillWidth: true
                    contentItem: Kit.LabeledSwitch {
                        label: qsTr("Share crash reports")
                        description: qsTr("Anonymous crash reports help fix bugs. Opt out any time.")
                        checked: App.crashReportingEnabled
                        onToggled: (checked) => App.setCrashReportingEnabled(checked)
                    }
                }
            }

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
                    Layout.topMargin: Tokens.s1
                    text: qsTr("Dish %1 · TinkerNorth · LGPL-3.0").arg(App.appVersion)
                    color: Theme.muted
                    font.family: Tokens.monoFamily
                    font.pixelSize: Tokens.textMeta
                }
            }
        }
    }
}
