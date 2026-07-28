// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Support / Donate page (design frame f-e3 "Support Dish") — reached from the
// rail's pulse-pink heart item and the Settings About row. A two-column grid:
// LEFT the hero (heart medallion, pulse eyebrow, headline, lead, the filled
// GitHub Sponsors CTA, the thanks line pinned low); RIGHT the three donation
// rails (GitHub Sponsors / Ko-fi / Buy Me a Coffee) and the "What your
// donation pays for" card. Straight from the Android donate surface: pulse
// pink (Theme.pulse, #FF6FB5 on navy, 12 % fills) is the ONE hue Dish uses
// beyond cyan, reserved for donations.
//
// Bound to the real `App` surface: App.donateSponsorsUrl / -KofiUrl / -BmacUrl
// (the brand defaults, localizable in C++) and App.openExternalUrl (routes
// through ExternalLink so a failure toasts). See docs/QML_CONTRACT.md §1b.

// Bind outer-component ids (donatePage) into the rail Repeater delegates.
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

    GridLayout {
        width: parent ? parent.width : implicitWidth
        // The design's 330px | flexible grid collapses to one column when the
        // content pane is narrow. Lower threshold than the Settings grid: the
        // hero column is FIXED at 330, so two columns fit once the flexible
        // rail column keeps a readable measure (~300px) — the design frame
        // renders two columns at the default window with the rail expanded.
        columns: donatePage.width < 660 ? 1 : 2
        columnSpacing: 26
        rowSpacing: 26

        // ── Hero (left column) ───────────────────────────────────────────────
        ColumnLayout {
            Layout.preferredWidth: 330
            Layout.fillWidth: donatePage.width < 760
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s5

            Rectangle {
                implicitWidth: 54
                implicitHeight: 54
                radius: 27
                color: Theme.pulseFill
                border.width: 1
                border.color: Theme.pulseEdge
                Label {
                    anchors.centerIn: parent
                    text: "♥"   // ♥ — brand glyph, not localized
                    color: Theme.pulse
                    font.pixelSize: 24
                }
            }

            Kit.Eyebrow {
                text: qsTr("Support the project")
                color: Theme.pulse
                Layout.topMargin: Tokens.s2
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Dish runs on coffee and goodwill.")
                color: Theme.onSurface
                font.pixelSize: 19
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Dish, Satellite, and every client are free, open source, ad-free, and "
                         + "analytics-free. No paywalled features, no upsells — donations are what "
                         + "keep them true.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                wrapMode: Text.WordWrap
            }

            // The filled pulse CTA (the design's one solid-pink control).
            AbstractButton {
                id: sponsorCta
                Layout.topMargin: Tokens.s3
                implicitHeight: 34
                implicitWidth: ctaRow.implicitWidth + 32
                hoverEnabled: true
                opacity: enabled ? 1.0 : Tokens.disabledOpacity
                onClicked: donatePage.openUrl(donatePage.urlSponsors)

                HoverHandler { cursorShape: Qt.PointingHandCursor }

                background: Rectangle {
                    radius: Tokens.radiusButton
                    color: sponsorCta.down || sponsorCta.hovered
                           ? Qt.darker(Theme.pulse, 1.15) : Theme.pulse
                }
                contentItem: Row {
                    id: ctaRow
                    spacing: Tokens.s4
                    leftPadding: Tokens.s8
                    rightPadding: Tokens.s8

                    Label {
                        text: "♥"   // ♥ — brand glyph, not localized
                        color: Theme.onPrimary
                        font.pixelSize: Tokens.textBase
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: qsTr("Sponsor on GitHub Sponsors")
                        color: Theme.onPrimary
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.DemiBold
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Label {
                Layout.fillWidth: true
                text: qsTr("Recommended — lowest fees, matched by GitHub where eligible.")
                color: Theme.muted
                font.pixelSize: Tokens.textChip
                wrapMode: Text.WordWrap
            }

            Label {
                Layout.fillWidth: true
                Layout.topMargin: Tokens.s8
                text: qsTr("Thank you. Every dollar, every star, every shared link. — Emir")
                color: Theme.muted
                font.pixelSize: Tokens.textMeta
                wrapMode: Text.WordWrap
            }
        }

        // ── Rails + the what-it-pays-for card (right column) ─────────────────
        ColumnLayout {
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignTop
            spacing: Tokens.s5

            Repeater {
                model: [
                    { name: "GitHub Sponsors", recommended: true,
                      blurb: qsTr("Monthly sponsorship, processed by GitHub. Lowest fees, no "
                                + "platform cut, and the most stable way to support Dish month "
                                + "after month."),
                      cadence: qsTr("Recurring"), currencies: qsTr("Card, PayPal, GitHub credit"),
                      openLabel: qsTr("Open GitHub Sponsors →"), url: donatePage.urlSponsors },
                    { name: "Ko-fi", recommended: false,
                      blurb: qsTr("Tip jar. Buy us a coffee, no account required — Ko-fi takes no "
                                + "cut on one-time tips."),
                      cadence: qsTr("One-time"),
                      currencies: qsTr("Card, Apple Pay, Google Pay, PayPal"),
                      openLabel: qsTr("Open Ko-fi →"), url: donatePage.urlKofi },
                    { name: "Buy Me a Coffee", recommended: false,
                      blurb: qsTr("Quick one-time gift or a monthly membership. Great if you don't "
                                + "have a GitHub account."),
                      cadence: qsTr("Either"), currencies: qsTr("Card, Apple Pay, Google Pay"),
                      openLabel: qsTr("Open Buy Me a Coffee →"), url: donatePage.urlBmac }
                ]
                delegate: Kit.Card {
                    id: railCard
                    required property var modelData
                    Layout.fillWidth: true
                    topPadding: 10
                    bottomPadding: 10

                    contentItem: ColumnLayout {
                        spacing: Tokens.s2

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: Tokens.s4

                            Label {
                                text: railCard.modelData.name
                                color: Theme.pulse
                                font.pixelSize: Tokens.textBase
                                font.weight: Font.DemiBold
                                Layout.fillWidth: true
                                elide: Text.ElideRight
                            }
                            // Recommended badge — filled pulse, mono caps.
                            Rectangle {
                                visible: railCard.modelData.recommended === true
                                radius: Tokens.radiusChip
                                color: Theme.pulse
                                implicitWidth: badgeText.implicitWidth + 14
                                implicitHeight: badgeText.implicitHeight + 4
                                Text {
                                    id: badgeText
                                    anchors.centerIn: parent
                                    text: qsTr("Recommended")
                                    color: Theme.onPrimary
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: 9
                                    font.letterSpacing: 1
                                    font.capitalization: Font.AllUppercase
                                }
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            text: railCard.modelData.blurb
                            color: Theme.muted
                            font.pixelSize: Tokens.textMeta
                            wrapMode: Text.WordWrap
                        }

                        // Cadence | Pays-with meta pair (muted mono micro-labels).
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.topMargin: Tokens.s3
                            spacing: 28

                            ColumnLayout {
                                spacing: Tokens.s1
                                Kit.Eyebrow { mutedTone: true; text: qsTr("Cadence") }
                                Label {
                                    text: railCard.modelData.cadence
                                    color: Theme.onSurface
                                    font.pixelSize: Tokens.textMeta
                                }
                            }
                            ColumnLayout {
                                spacing: Tokens.s1
                                Kit.Eyebrow { mutedTone: true; text: qsTr("Pays with") }
                                Label {
                                    text: railCard.modelData.currencies
                                    color: Theme.onSurface
                                    font.pixelSize: Tokens.textMeta
                                }
                            }
                            Item { Layout.fillWidth: true }
                        }

                        // Visible visit link, keyboard/screen-reader reachable.
                        Label {
                            Layout.topMargin: Tokens.s3
                            text: railCard.modelData.openLabel
                            color: Theme.pulse
                            font.pixelSize: Tokens.textSummary
                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: donatePage.openUrl(railCard.modelData.url)
                            }
                        }
                    }

                    // Whole-card click opens the same URL.
                    MouseArea {
                        anchors.fill: parent
                        z: -1
                        cursorShape: Qt.PointingHandCursor
                        onClicked: donatePage.openUrl(railCard.modelData.url)
                    }
                }
            }

            // "What your donation pays for" — the design's condensed card with
            // bold leads inline (StyledText; the leads read in onSurface).
            Kit.Card {
                Layout.fillWidth: true
                topPadding: 10
                bottomPadding: 10

                contentItem: ColumnLayout {
                    spacing: Tokens.s3

                    Label {
                        Layout.fillWidth: true
                        text: qsTr("What your donation pays for")
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textBase
                        font.weight: Font.DemiBold
                        wrapMode: Text.WordWrap
                    }
                    Label {
                        Layout.fillWidth: true
                        textFormat: Text.StyledText
                        text: qsTr("%1Hosting%2 — dish.tinkernorth.com and every signed-installer "
                                 + "mirror. %1Code signing%2 — the EV certificate that keeps "
                                 + "SmartScreen friendly, renewed yearly. %1Store fees%2 — "
                                 + "listings and policy upkeep. %1Time%2 — honest answer: most "
                                 + "of it.")
                              .arg("<b><font color=\"" + Theme.onSurface + "\">")
                              .arg("</font></b>")
                        color: Theme.muted
                        font.pixelSize: Tokens.textMeta
                        lineHeight: 1.4
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    // Route through App.openExternalUrl so a failed open raises the same toast
    // the Widgets DonateView did (via ExternalLink), not a silent
    // Qt.openUrlExternally.
    function openUrl(url) {
        if (url && url.length > 0) {
            App.openExternalUrl(url);
        }
    }
}
