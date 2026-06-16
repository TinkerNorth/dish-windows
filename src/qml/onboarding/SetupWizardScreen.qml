// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Second onboarding screen: the setup guide (mirrors the Widgets SetupWizardView
// — a "Step n of m" progress label, an eyebrow/title/body, and a pair of option
// cards per step). Three steps: Connect, Controller, Let's go. Back from step 1
// returns to the Welcome pager; Finish on the last step advances to Help. Skip
// leaves onboarding. Presentation only — no paging logic beyond the step index.

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

    signal back()      // step 0 Back -> previous screen (Welcome)
    signal finished()  // last-step Finish -> next screen (Help)
    signal skip()      // leave onboarding

    // Each step: eyebrow + title + body, then two option cards (title/body, the
    // first stroked = the highlighted "do this" card, parity with the Widgets
    // primary-stroke selection look).
    ListModel {
        id: steps
        ListElement {
            eyebrow: QT_TR_NOOP("STEP 1 · CONNECT")
            title: QT_TR_NOOP("Find and pair your Satellite")
            body: QT_TR_NOOP("Dish reaches a host PC over your local network. Both machines must be on the same Wi-Fi or LAN.")
            cardATitle: QT_TR_NOOP("Find your PC on the network")
            cardABody: QT_TR_NOOP("Open Connections and tap Scan. Satellites running on your LAN appear automatically — no IP to type in.")
            cardBTitle: QT_TR_NOOP("Enter the operator PIN")
            cardBBody: QT_TR_NOOP("Pick your Satellite and enter the 6-digit PIN it shows on the host screen. Once accepted, the pairing is remembered.")
        }
        ListElement {
            eyebrow: QT_TR_NOOP("STEP 2 · CONTROLLER")
            title: QT_TR_NOOP("Plug in or pair a controller")
            body: QT_TR_NOOP("There is no on-screen pad to pick on Windows — Dish forwards any real controller this PC sees.")
            cardATitle: QT_TR_NOOP("It appears automatically")
            cardABody: QT_TR_NOOP("Connect an Xbox, PlayStation, or generic gamepad over USB or Bluetooth. Windows detects it and Dish lists it as a new slot.")
            cardBTitle: QT_TR_NOOP("Confirm it's detected")
            cardBBody: QT_TR_NOOP("The controller shows up on the dashboard with its capabilities (motion, lightbar, battery). Bind it to a paired Satellite to start streaming.")
        }
        ListElement {
            eyebrow: QT_TR_NOOP("STEP 3 · LET'S GO")
            title: QT_TR_NOOP("Here's what we'll do")
            body: ""
            cardATitle: QT_TR_NOOP("Summary")
            cardABody: QT_TR_NOOP("Connection: Wi-Fi / LAN to Satellite.\nController: any pad this PC detects.")
            cardBTitle: QT_TR_NOOP("What's next")
            cardBBody: QT_TR_NOOP("Tap Finish to open Connections. Scan for your Satellite, pair it with the PIN, then bind a connected controller to start playing.")
        }
    }

    readonly property int stepCount: steps.count
    readonly property bool onFinalStep: pager.currentIndex === stepCount - 1

    // One option card: title + body with a primary stroke when highlighted.
    component OptionCard: Kit.Card {
        id: optionCard
        property string heading: ""
        property string detail: ""
        property bool highlighted: false

        Layout.fillWidth: true
        background: Rectangle {
            radius: 12
            color: Theme.surface
            border.width: optionCard.highlighted ? 2 : 1
            border.color: optionCard.highlighted ? Theme.primary : Theme.outline
        }
        contentItem: ColumnLayout {
            spacing: 4
            Label {
                Layout.fillWidth: true
                text: optionCard.heading
                color: Theme.onSurface
                font.pixelSize: 14
                font.bold: true
                wrapMode: Text.WordWrap
            }
            Label {
                Layout.fillWidth: true
                text: optionCard.detail
                color: Theme.muted
                font.pixelSize: 12
                wrapMode: Text.WordWrap
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 24
        spacing: 14

        Kit.SectionHeader {
            label: qsTr("Step %1 of %2").arg(pager.currentIndex + 1).arg(screen.stepCount)
        }

        SwipeView {
            id: pager
            Layout.fillWidth: true
            Layout.fillHeight: true
            interactive: false
            clip: true

            Repeater {
                model: steps
                delegate: ColumnLayout {
                    id: stepDelegate
                    width: pager.width
                    height: pager.height
                    spacing: 10

                    required property string eyebrow
                    required property string title
                    required property string body
                    required property string cardATitle
                    required property string cardABody
                    required property string cardBTitle
                    required property string cardBBody

                    Kit.SectionHeader { label: qsTr(stepDelegate.eyebrow) }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr(stepDelegate.title)
                        color: Theme.onSurface
                        font.pixelSize: 17
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        visible: stepDelegate.body.length > 0
                        text: stepDelegate.body.length > 0 ? qsTr(stepDelegate.body) : ""
                        color: Theme.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                    OptionCard {
                        heading: qsTr(stepDelegate.cardATitle)
                        detail: qsTr(stepDelegate.cardABody)
                        highlighted: true
                    }
                    OptionCard {
                        heading: qsTr(stepDelegate.cardBTitle)
                        detail: qsTr(stepDelegate.cardBBody)
                        highlighted: false
                    }
                    Item { Layout.fillHeight: true }
                }
            }
        }

        OnboardingNavBar {
            Layout.fillWidth: true
            primaryText: screen.onFinalStep ? qsTr("Finish") : qsTr("Next")
            onBackClicked: {
                if (pager.currentIndex > 0)
                    pager.decrementCurrentIndex()
                else
                    screen.back()
            }
            onSkipClicked: screen.skip()
            onPrimaryClicked: {
                if (screen.onFinalStep)
                    screen.finished()
                else
                    pager.incrementCurrentIndex()
            }
        }
    }
}
