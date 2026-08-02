// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The 4-page first-run welcome pager (design flow 01): a centered hero (brand
// mark on page one, eyebrow, title, body), step dots pinned near the bottom,
// and a Back / Skip / Next bar in the bottom-right — over the bare window
// surface with no rail (the shell appears when onboarding pops).
//
// Both exits are completions. Skip and Finish each emit finished(); only the
// hand-off differs — Finish opens the setup wizard, Skip lands on Home. (A Skip
// that did not persist the flag re-showed the whole welcome on every launch.)

pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Item {
    id: screen

    // Leave onboarding. runSetup=true also opens the setup wizard over the
    // shell (the final page's primary action); Skip leaves without it. Either
    // way the host marks onboarding complete.
    signal finished(bool runSetup)

    // Satellite download URL. Kept as a property so a localized override can
    // swap it without touching layout (parity with the Widgets url_satellite).
    readonly property string satelliteUrl: "https://dish.tinkernorth.com/downloads/satellite"

    // cta: "" none, "satellite" opens the download page.
    ListModel {
        id: pages
        ListElement {
            eyebrow: QT_TR_NOOP("Welcome")
            title: QT_TR_NOOP("Your controller, on your PC")
            body: QT_TR_NOOP("Dish forwards a real game controller plugged into this PC to another PC running Satellite, over your local network. Plug in a pad and it shows up ready to play.")
            ctaLabel: ""
            showMark: true
        }
        ListElement {
            eyebrow: QT_TR_NOOP("How it works")
            title: QT_TR_NOOP("A short hop over Wi-Fi")
            body: QT_TR_NOOP("Your controller’s button presses, sticks, and motion travel to a small free helper called Satellite running on the host PC. Satellite shows up to its games as a regular gamepad. No extra setup per game.")
            ctaLabel: ""
            showMark: false
        }
        ListElement {
            eyebrow: QT_TR_NOOP("One more thing")
            title: QT_TR_NOOP("Install Satellite on the host PC")
            body: QT_TR_NOOP("Satellite is free and open source. Grab it from dish.tinkernorth.com, run the installer, and Dish will find it automatically. You can finish this intro now and install Satellite later.")
            ctaLabel: QT_TR_NOOP("Open download page")
            showMark: false
        }
        ListElement {
            eyebrow: QT_TR_NOOP("You’re set")
            title: QT_TR_NOOP("Ready when you are")
            body: QT_TR_NOOP("Finish walks you through input, destination and binding — about a minute. You can also skip and do it later from Set up in the rail.")
            ctaLabel: ""
            showMark: false
        }
    }

    property int currentIndex: 0
    readonly property int pageCount: pages.count
    readonly property bool onFinalPage: currentIndex === pageCount - 1
    readonly property var currentPage: pages.get(currentIndex)

    // Esc leaves the same way Skip does — the flow is never a trap, and the
    // exit is still a completion.
    focus: true
    Keys.onEscapePressed: function (event) {
        screen.finished(false);
        event.accepted = true;
    }

    // The primary is the first thing a new user ever sees; give it the keyboard
    // and a real focus ring rather than an invisible default.
    Component.onCompleted: primaryButton.forceActiveFocus(Qt.TabFocusReason)

    // ── Hero: centered column, pulled slightly above optical center. ─────────
    ColumnLayout {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -Tokens.s9
        width: Math.min(460, parent.width - 80)
        spacing: Tokens.s6

        Kit.BrandGlyph {
            glyph: "dish-master"
            Layout.preferredWidth: Tokens.glyphHero
            Layout.preferredHeight: Tokens.glyphHero
            Layout.alignment: Qt.AlignHCenter
            visible: screen.currentPage.showMark
        }
        Kit.Eyebrow {
            text: qsTr(screen.currentPage.eyebrow)
            Layout.alignment: Qt.AlignHCenter
        }
        Label {
            text: qsTr(screen.currentPage.title)
            color: Theme.onSurface
            font.pixelSize: Tokens.textHero
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Label {
            text: qsTr(screen.currentPage.body)
            color: Theme.muted
            font.pixelSize: Tokens.textBase
            lineHeight: 1.55
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Kit.DishButton {
            text: screen.currentPage.ctaLabel.length > 0
                  ? qsTr(screen.currentPage.ctaLabel) : ""
            visible: screen.currentPage.ctaLabel.length > 0
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: Tokens.s2
            onClicked: App.openExternalUrl(screen.satelliteUrl)
        }
    }

    // ── Step dots. ───────────────────────────────────────────────────────────
    Row {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Tokens.s11 * 2
        spacing: Tokens.s4

        Repeater {
            model: screen.pageCount
            delegate: Rectangle {
                required property int index
                width: Tokens.dotSize
                height: Tokens.dotSize
                radius: Tokens.dotSize / 2
                color: index === screen.currentIndex ? Theme.primary : Theme.surfaceDim
                border.width: index === screen.currentIndex ? 0 : 1
                border.color: Theme.outline
            }
        }
    }

    // ── Bottom bar: Back (past page one) · spacer · Skip · Next/Finish. ──────
    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Tokens.pagePadding
        anchors.rightMargin: Tokens.pagePadding
        anchors.bottomMargin: Tokens.s8
        spacing: Tokens.s4

        Kit.DishButton {
            text: qsTr("Back")
            visible: screen.currentIndex > 0
            onClicked: screen.currentIndex -= 1
        }
        Item { Layout.fillWidth: true }
        Kit.DishButton {
            text: qsTr("Skip")
            onClicked: screen.finished(false)
        }
        Kit.DishButton {
            id: primaryButton
            variant: Kit.DishButton.Primary
            text: screen.onFinalPage ? qsTr("Finish") : qsTr("Next")
            onClicked: {
                if (screen.onFinalPage)
                    screen.finished(true);
                else
                    screen.currentIndex += 1;
            }
        }
    }
}
