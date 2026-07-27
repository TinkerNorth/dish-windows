// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Support / Donate detail sub-page (pushed from SettingsPage). Mirrors the
// Widgets DonateView: a centered hero (heart, eyebrow, headline, lead, primary
// CTA), three donation-rail cards (GitHub Sponsors / Ko-fi / Buy Me a Coffee)
// each clickable with an "Open X →" link and a Cadence | Pays-with meta grid, a
// "What your donation pays for" card, and a closing thanks line.
//
// Bound to the real `App` surface: App.donateSponsorsUrl / -KofiUrl / -BmacUrl
// (the brand defaults, localizable in C++) and App.openExternalUrl (routes
// through ExternalLink so a failure toasts). See docs/QML_CONTRACT.md §1b.

// Bind outer-component ids (donatePage) into the rail/why Repeater delegates.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    id: donatePage
    title: qsTr("Support Dish")

    readonly property string headerTitle: qsTr("Support Dish")
    readonly property string headerSub: qsTr("Dish is free and open source — donations keep it that way")

    // Brand donation URLs from the App surface (defaults mirror DonateView; the
    // C++ side carries the localizable overrides).
    readonly property string urlSponsors: App.donateSponsorsUrl
    readonly property string urlKofi: App.donateKofiUrl
    readonly property string urlBmac: App.donateBmacUrl

    // ── Hero (centered) ──────────────────────────────────────────────────────
    // The pulse-pink accent has no portable Theme token; the heart / eyebrow /
    // CTA use the cyan `primary` accent (the same substitution DonateView makes).
    ColumnLayout {
        width: parent ? parent.width : implicitWidth
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: 64
            implicitHeight: 64
            radius: 32
            // Faint primary tint behind the heart (mirrors tokenFill(primary, 0.14)).
            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.14)
            Label {
                anchors.centerIn: parent
                text: "♥"   // ♥ — brand glyph, not localized
                color: Theme.primary
                font.pixelSize: 30
            }
        }

        Kit.SectionHeader {
            Layout.alignment: Qt.AlignHCenter
            label: qsTr("Support the project")
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Dish runs on coffee and goodwill.")
            color: Theme.onSurface
            font.pixelSize: 18
            font.bold: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Label {
            Layout.fillWidth: true
            text: qsTr("Dish, Satellite, and every Dish client are free, open source, ad-free, and "
                     + "analytics-free. No paywalled features, no upsells. Donations are what keep "
                     + "them that way.")
            color: Theme.muted
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
        }

        Kit.KitButton {
            Layout.alignment: Qt.AlignHCenter
            text: "♥  " + qsTr("Sponsor on GitHub Sponsors (recommended)")
            onClicked: donatePage.openUrl(donatePage.urlSponsors)
        }
    }

    // ── Rails ────────────────────────────────────────────────────────────────
    Column {
        spacing: 12
        width: parent ? parent.width : implicitWidth

        Repeater {
            model: [
                { name: "GitHub Sponsors", recommended: true,
                  blurb: qsTr("Monthly sponsorship, processed by GitHub. Lowest fees, no platform "
                            + "cut — the most stable way to support Dish, and the one GitHub "
                            + "matches dollar-for-dollar where eligible."),
                  cadence: qsTr("Recurring"), currencies: qsTr("Card, PayPal, GitHub credit"),
                  openLabel: qsTr("Open GitHub Sponsors →"), url: donatePage.urlSponsors },
                { name: "Ko-fi", recommended: false,
                  blurb: qsTr("Tip jar. Buy us a coffee, no account required. Ko-fi takes no cut on "
                            + "one-time tips, so every dollar reaches the project."),
                  cadence: qsTr("One-time"), currencies: qsTr("Card, Apple Pay, Google Pay, PayPal"),
                  openLabel: qsTr("Open Ko-fi →"), url: donatePage.urlKofi },
                { name: "Buy Me a Coffee", recommended: false,
                  blurb: qsTr("Quick one-time gift or a monthly membership. Card, Apple Pay, or "
                            + "Google Pay. Great if you don't have a GitHub account."),
                  cadence: qsTr("Either"), currencies: qsTr("Card, Apple Pay, Google Pay"),
                  openLabel: qsTr("Open Buy Me a Coffee →"), url: donatePage.urlBmac }
            ]
            delegate: Kit.Card {
                id: railCard
                required property var modelData
                width: parent ? parent.width : implicitWidth

                contentItem: ColumnLayout {
                    spacing: 8

                    RowLayout {
                        spacing: 8
                        Label {
                            text: railCard.modelData.name
                            color: Theme.primary
                            font.pixelSize: 15
                            font.bold: true
                        }
                        // Recommended badge (mirrors capabilityChipQss(true)).
                        Rectangle {
                            visible: railCard.modelData.recommended === true
                            radius: 6
                            color: Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.14)
                            border.width: 1
                            border.color: Theme.primary
                            implicitWidth: badgeText.implicitWidth + 14
                            implicitHeight: badgeText.implicitHeight + 6
                            Label {
                                id: badgeText
                                anchors.centerIn: parent
                                text: qsTr("Recommended")
                                color: Theme.primary
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Label {
                        Layout.fillWidth: true
                        text: railCard.modelData.blurb
                        color: Theme.muted
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }

                    // Two-column Cadence | Pays-with meta grid.
                    RowLayout {
                        spacing: 12
                        Layout.fillWidth: true
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: qsTr("Cadence")
                                color: Theme.warning
                                font.family: "Consolas"
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1
                            }
                            Label {
                                Layout.fillWidth: true
                                text: railCard.modelData.cadence
                                color: Theme.onSurface
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Label {
                                text: qsTr("Pays with")
                                color: Theme.warning
                                font.family: "Consolas"
                                font.pixelSize: 10
                                font.bold: true
                                font.letterSpacing: 1
                            }
                            Label {
                                Layout.fillWidth: true
                                text: railCard.modelData.currencies
                                color: Theme.onSurface
                                font.pixelSize: 12
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // Visible visit link (accent), keyboard/screen-reader accessible.
                    Label {
                        text: railCard.modelData.openLabel
                        color: Theme.primary
                        font.pixelSize: 12
                        font.bold: true
                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: donatePage.openUrl(railCard.modelData.url)
                        }
                    }
                }

                // Whole-card click opens the same URL (mirrors CardClickFilter).
                MouseArea {
                    anchors.fill: parent
                    z: -1
                    cursorShape: Qt.PointingHandCursor
                    onClicked: donatePage.openUrl(railCard.modelData.url)
                }
            }
        }
    }

    // ── "What your donation pays for" card ───────────────────────────────────
    Kit.Card {
        width: parent ? parent.width : implicitWidth
        contentItem: ColumnLayout {
            spacing: 10

            Label {
                Layout.fillWidth: true
                text: qsTr("What your donation pays for")
                color: Theme.onSurface
                font.pixelSize: 15
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Repeater {
                model: [
                    qsTr("Hosting. dish.tinkernorth.com, tinkernorth.com, and every signed-installer "
                       + "mirror. AWS isn't free, even at our scale."),
                    qsTr("Code-signing certificates. Windows SmartScreen only plays nice because we "
                       + "pay for an EV certificate every year."),
                    qsTr("Store developer fees. The platform accounts and the time to keep each "
                       + "listing compliant with every new policy round."),
                    qsTr("Time. Honest answer: most of it. Dish is a nights-and-weekends project. "
                       + "Donations let us say yes to working on it.")
                ]
                delegate: RowLayout {
                    id: whyRow
                    required property string modelData
                    Layout.fillWidth: true
                    spacing: 10
                    Kit.StatusDot {
                        token: "primary"
                        Layout.alignment: Qt.AlignTop
                        Layout.topMargin: 5
                    }
                    Label {
                        Layout.fillWidth: true
                        text: whyRow.modelData
                        color: Theme.onSurface
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // ── Thanks (centered) ────────────────────────────────────────────────────
    Label {
        text: qsTr("Thank you. Every dollar, every star, every shared link. They all add up. Emir")
        color: Theme.muted
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.WordWrap
        width: parent ? parent.width : implicitWidth
    }

    // Route through App.openExternalUrl so a failed open raises the same toast the
    // Widgets DonateView does (via ExternalLink), not a silent Qt.openUrlExternally.
    function openUrl(url) {
        if (url && url.length > 0) {
            App.openExternalUrl(url);
        }
    }
}
