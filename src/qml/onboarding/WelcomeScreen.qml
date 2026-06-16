// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// First onboarding screen: the welcome pager (mirrors the Widgets WelcomeDialog
// — four eyebrow/title/body pages with step-indicator dots and Back/Skip/Next).
// The final page's Next ("Get started") advances the flow to the Setup guide;
// Skip leaves onboarding entirely. The two CTA pages open an external page
// (Satellite download) and jump straight to the guide respectively.
//
// Copy is held inline as a ListModel of plain spec rows — there is no logic
// here, only presentation. (Hero art is omitted: no bundled onboarding glyphs,
// same as the Widgets port.)

// ComponentBehavior: Bound — Repeater delegates reference outer ids (pager,
// screen); binding keeps that access qualified (qmllint-clean).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Item {
    id: screen

    // Advance to the Setup guide (final-page Next, or the "Open setup guide" CTA).
    signal advance()
    // Leave onboarding (Skip).
    signal skip()

    // Satellite download URL. Kept as a property so a localized override can swap
    // it without touching layout (parity with the Widgets url_satellite string).
    readonly property string satelliteUrl: "https://tinkernorth.com/satellite"

    // cta: "" none, "satellite" opens the download page, "wizard" jumps to guide.
    ListModel {
        id: pages
        ListElement {
            eyebrow: QT_TR_NOOP("WELCOME")
            title: QT_TR_NOOP("Your controller, on your PC")
            body: QT_TR_NOOP("Dish forwards a real game controller plugged into this PC to another PC running Satellite, over your local network. Plug in a pad and it shows up ready to play.")
            ctaLabel: ""
            cta: ""
        }
        ListElement {
            eyebrow: QT_TR_NOOP("HOW IT WORKS")
            title: QT_TR_NOOP("A short hop over Wi-Fi")
            body: QT_TR_NOOP("Your controller's button presses, sticks, and motion travel to a small free helper called Satellite running on the host PC. Satellite shows up to its games as a regular gamepad. No extra setup per game.")
            ctaLabel: ""
            cta: ""
        }
        ListElement {
            eyebrow: QT_TR_NOOP("ONE MORE THING")
            title: QT_TR_NOOP("Install Satellite on the host PC")
            body: QT_TR_NOOP("Satellite is free and open source. Grab it from tinkernorth.com/satellite, run the installer, and Dish will find it automatically. You can finish this intro now and install Satellite later.")
            ctaLabel: QT_TR_NOOP("Open download page")
            cta: "satellite"
        }
        ListElement {
            eyebrow: QT_TR_NOOP("YOU'RE SET")
            title: QT_TR_NOOP("Ready when you are")
            body: QT_TR_NOOP("Open the setup guide for a walkthrough, or jump straight in and pair with a Satellite on your network.")
            ctaLabel: QT_TR_NOOP("Open setup guide")
            cta: "wizard"
        }
    }

    readonly property int pageCount: pages.count
    readonly property bool onFinalPage: pager.currentIndex === pageCount - 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 16

        // The pager card carries the content for the active page on the Mica
        // surface; SwipeView is kept non-interactive so paging is button-driven
        // (parity with the Widgets QStackedWidget pager).
        Kit.Card {
            Layout.fillWidth: true
            Layout.fillHeight: true

            contentItem: SwipeView {
                id: pager
                interactive: false
                clip: true

                Repeater {
                    model: pages
                    delegate: ColumnLayout {
                        id: pageDelegate
                        // SwipeView sizes its pages; fill it.
                        width: pager.width
                        height: pager.height
                        spacing: 10

                        required property string eyebrow
                        required property string title
                        required property string body
                        required property string ctaLabel
                        required property string cta

                        Item { Layout.fillHeight: true }

                        Kit.SectionHeader {
                            Layout.alignment: Qt.AlignHCenter
                            label: qsTr(pageDelegate.eyebrow)
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr(pageDelegate.title)
                            color: Theme.onSurface
                            font.pixelSize: 20
                            font.bold: true
                        }
                        Label {
                            Layout.fillWidth: true
                            horizontalAlignment: Text.AlignHCenter
                            wrapMode: Text.WordWrap
                            text: qsTr(pageDelegate.body)
                            color: Theme.muted
                            font.pixelSize: 13
                        }
                        Kit.OutlineButton {
                            Layout.alignment: Qt.AlignHCenter
                            visible: pageDelegate.ctaLabel.length > 0
                            text: pageDelegate.ctaLabel.length > 0 ? qsTr(pageDelegate.ctaLabel) : ""
                            onClicked: {
                                if (pageDelegate.cta === "satellite")
                                    Qt.openUrlExternally(screen.satelliteUrl)
                                else if (pageDelegate.cta === "wizard")
                                    screen.advance()
                            }
                        }

                        Item { Layout.fillHeight: true }
                    }
                }
            }
        }

        // Step-indicator dots.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 8
            Repeater {
                model: screen.pageCount
                delegate: Rectangle {
                    required property int index
                    width: 8
                    height: 8
                    radius: 4
                    color: index === pager.currentIndex ? Theme.primary : Theme.muted
                }
            }
        }

        OnboardingNavBar {
            Layout.fillWidth: true
            backVisible: pager.currentIndex > 0
            skipVisible: !screen.onFinalPage
            primaryText: screen.onFinalPage ? qsTr("Get started") : qsTr("Next")
            onBackClicked: pager.decrementCurrentIndex()
            onSkipClicked: screen.skip()
            onPrimaryClicked: {
                if (screen.onFinalPage)
                    screen.advance()
                else
                    pager.incrementCurrentIndex()
            }
        }
    }
}
