// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Help & FAQ — a PUSHED DETAIL PAGE (Settings ▸ Setup & help ▸ Help & FAQ). It
// is no longer an onboarding step: it declares headerTitle/headerSub for the
// shell breadcrumb, gets the shell's back chevron for free, and scrolls inside
// Kit.Page's single page-level scroller. It carries no footer buttons of its
// own — an unspecified surface behind a Settings row is how one screen gets
// built three different ways.
//
// A scrollable list of section headers, collapsible question/answer rows and
// external-link buttons (mirrors the Widgets HelpView). Presentation only; the
// only binding is App.openExternalUrl, which routes a failed launch to a toast.

// ComponentBehavior: Bound — the Repeater delegate's inline Components reference
// the outer `row`/`screen` ids; binding keeps that access qualified.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: screen
    title: qsTr("Help & FAQ")

    readonly property string headerTitle: qsTr("Help & FAQ")
    readonly property string headerSub: qsTr("How Dish works, and what to try when it doesn’t")

    readonly property string privacyUrl: "https://dish.tinkernorth.com/privacy/dish-windows/"
    readonly property string githubUrl: "https://github.com/TinkerNorth"

    // The FAQ content. `kind`: "section" = a header row; "faq" = a collapsible
    // question/answer; "link" = an outline button opening `url`.
    ListModel {
        id: items
        ListElement { kind: "section"; heading: QT_TR_NOOP("CONCEPTS") }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("What is Dish?")
            answer: QT_TR_NOOP("Dish turns a controller plugged into this PC into a wireless gamepad for another PC. It forwards input over your local network to the free Satellite helper.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("What is Satellite?")
            answer: QT_TR_NOOP("Satellite is a small, free, open-source program that runs on the host PC. It receives input from Dish over Wi-Fi and presents itself to that PC’s games as a regular controller. Without Satellite, games can’t see your controller. Install it from tinkernorth.com/satellite.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("What’s motion and what’s the touchpad?")
            answer: QT_TR_NOOP("Motion is a controller’s gyroscope used as aim assist, forwarded as the gyro inside a PlayStation pad. The touchpad forwards a DualSense / DualShock 4 touchpad. Both need a PlayStation-style controller and a Satellite connection.")
        }

        ListElement { kind: "section"; heading: QT_TR_NOOP("BEST PERFORMANCE") }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("What’s the best setup for low latency?")
            answer: QT_TR_NOOP("Both PCs on the same router, wired Ethernet where you can, 5 GHz Wi-Fi otherwise. Dish needs this PC and the host PC on the same local network — it sends input as small UDP packets, so a fast LAN keeps the delay tiny.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("Is wired better than wireless?")
            answer: QT_TR_NOOP("For the controller-to-PC leg, a USB controller has no radio latency. For the PC-to-PC leg, connect both machines to the router with Ethernet if you can.")
        }

        ListElement { kind: "section"; heading: QT_TR_NOOP("TROUBLESHOOTING") }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("Dish can’t find any Satellites. What now?")
            answer: QT_TR_NOOP("Check that Satellite is running on the host PC (its tray icon should be visible). Confirm both PCs are on the same network. Guest networks and 2.4 GHz vs 5 GHz on the same router count as different. Tap Scan in Connections to retry.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("My PIN keeps getting rejected.")
            answer: QT_TR_NOOP("The PIN is shown by Satellite on the host PC and changes if you restart it. Check the digits right before you type, and make sure you’re pairing with the right Satellite if more than one is listed.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("My controller keeps disconnecting mid-game.")
            answer: QT_TR_NOOP("Most disconnects come from Wi-Fi switching bands or the controller’s own wireless dropping. Prefer wired Ethernet on both PCs and a wired controller. Dish keeps the session alive while the app is open.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("Motion (gyro aim) isn’t working.")
            answer: QT_TR_NOOP("Motion needs three things: a Satellite connection, a controller with a gyroscope (DualSense, DualShock 4, Switch Pro), and a PlayStation-style controller type on the slot. Switch the emulated type to PlayStation to enable the motion channel.")
        }

        ListElement { kind: "section"; heading: QT_TR_NOOP("ABOUT DISH") }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("Is Dish open source?")
            answer: QT_TR_NOOP("Yes. Dish (Windows, Android, Linux, Mac) and Satellite are all released under LGPL-3.0, source on GitHub at github.com/TinkerNorth. Pull requests welcome.")
        }
        ListElement {
            kind: "faq"
            heading: QT_TR_NOOP("What does Dish collect?")
            answer: QT_TR_NOOP("Almost nothing. Dish doesn’t send gameplay or controller input anywhere except the host PC on your local network. Anonymous crash reports are on by default and can be turned off (Settings → Diagnostics). See the privacy policy for the full list.")
        }

        ListElement { kind: "section"; heading: QT_TR_NOOP("LINKS") }
        ListElement { kind: "link"; heading: QT_TR_NOOP("Privacy policy"); url: "privacy" }
        ListElement { kind: "link"; heading: QT_TR_NOOP("View source on GitHub"); url: "github" }
    }

    // One content-sized column inside the page scroller — never a second,
    // nested scroll region. Capped to a readable measure.
    Column {
        id: helpBody
        width: Math.min(parent ? parent.width : 640, 640)
        spacing: Tokens.s2

        Repeater {
            model: items

            delegate: Item {
                id: row
                required property string kind
                required property string heading
                // Optional roles (present only for some kinds); default to "".
                required property var answer
                required property var url

                width: helpBody.width
                // Loader.implicitHeight tracks its loaded Item's implicit size,
                // so the row sizes to its content.
                implicitHeight: loader.implicitHeight

                Loader {
                    id: loader
                    width: row.width
                    sourceComponent: row.kind === "section" ? sectionRow
                                   : row.kind === "link" ? linkRow
                                   : faqRow
                }

                Component {
                    id: sectionRow
                    Kit.SectionHeader {
                        topPadding: Tokens.s5
                        bottomPadding: Tokens.s1
                        label: qsTr(row.heading)
                    }
                }

                Component {
                    id: faqRow
                    ColumnLayout {
                        spacing: Tokens.s1

                        // A checkable header that toggles its answer body.
                        Button {
                            id: header
                            Layout.fillWidth: true
                            Layout.minimumHeight: Tokens.hitRow
                            checkable: true
                            padding: Tokens.s3
                            focusPolicy: Qt.StrongFocus

                            Accessible.role: Accessible.Button
                            Accessible.name: qsTr(row.heading)

                            contentItem: RowLayout {
                                spacing: Tokens.s3
                                Text {
                                    // A rotated guillemet is the disclosure
                                    // affordance; the solid triangles have no
                                    // reliable glyph across Windows faces.
                                    text: "›"
                                    font.family: Tokens.sansFamily
                                    font.pixelSize: Tokens.textSummary
                                    color: Theme.muted
                                    rotation: header.checked ? 90 : 0
                                    Behavior on rotation {
                                        NumberAnimation {
                                            duration: Tokens.reducedMotion ? 0 : Tokens.durFast
                                        }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr(row.heading)
                                    color: Theme.onSurface
                                    font.pixelSize: Tokens.textBase
                                    font.bold: true
                                    wrapMode: Text.WordWrap
                                }
                            }

                            background: Rectangle {
                                radius: Tokens.radiusButton
                                color: header.hovered ? Theme.primaryHover : "transparent"
                                border.width: header.visualFocus ? 1 : 0
                                border.color: Theme.primary

                                Rectangle {
                                    visible: header.visualFocus
                                    anchors.fill: parent
                                    anchors.margins: -Tokens.s1
                                    radius: Tokens.radiusButton + Tokens.s1
                                    color: "transparent"
                                    border.width: 2
                                    border.color: Theme.focusRing
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            Layout.leftMargin: Tokens.s2
                            Layout.bottomMargin: Tokens.s4
                            visible: header.checked
                            text: qsTr(String(row.answer))
                            color: Theme.muted
                            font.pixelSize: Tokens.textSummary
                            lineHeight: 1.5
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                Component {
                    id: linkRow
                    Row {
                        topPadding: Tokens.s2
                        Kit.DishButton {
                            text: qsTr(row.heading)
                            onClicked: {
                                if (String(row.url) === "privacy")
                                    App.openExternalUrl(screen.privacyUrl);
                                else if (String(row.url) === "github")
                                    App.openExternalUrl(screen.githubUrl);
                            }
                        }
                    }
                }
            }
        }
    }
}
