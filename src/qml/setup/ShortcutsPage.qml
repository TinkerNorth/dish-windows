// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 3: the on-top choices plus the no-junk promise. The primary here is
// "Install" — no ellipsis, no chevron: it acts now. primaryActivated()
// returns false and hands the commit to the host, which runs the downgrade
// confirm (when needed) before Setup.beginInstall(); a declined UAC lands
// back here as an ErrorBanner (diagnosis + retry), never a dialog loop.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    property bool elevationDeclined: false

    readonly property string hint: Setup.needsElevation ? qsTr("Windows will ask for approval.")
                                                        : qsTr("Nothing is copied yet.")
    readonly property string primaryLabel: qsTr("Install")
    readonly property bool canAdvance: true

    signal installRequested()

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function primaryActivated() {
        page.elevationDeclined = false;
        page.installRequested();
        return false; // the phase change advances the step, not the footer
    }

    function activated() {
        page.elevationDeclined = false;
    }

    function showElevationDeclined() {
        page.elevationDeclined = true;
    }

    ColumnLayout {
        id: pageColumn
        anchors.fill: parent
        spacing: Tokens.s6

        ColumnLayout {
            spacing: Tokens.s1
            Layout.fillWidth: true

            Label {
                id: heading
                text: qsTr("Where Dish shows up")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Pick the places Dish should appear. Both can be removed later without reinstalling.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        ColumnLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Kit.LabeledSwitch {
                label: qsTr("Start Menu")
                description: qsTr("Dish in the Start Menu app list.")
                checked: Setup.wantStartMenu
                onToggled: function (checked) { Setup.wantStartMenu = checked; }
                Layout.fillWidth: true
            }
            Rectangle {
                implicitHeight: 1
                color: Theme.outlineSubtle
                Layout.fillWidth: true
            }
            Kit.LabeledSwitch {
                label: qsTr("Desktop")
                description: qsTr("A Dish shortcut on the desktop.")
                checked: Setup.wantDesktop
                onToggled: function (checked) { Setup.wantDesktop = checked; }
                Layout.fillWidth: true
            }
        }

        Kit.ErrorBanner {
            id: elevationBanner
            visible: page.elevationDeclined
            text: qsTr("Windows didn’t approve the install. Try again, or go back and pick Just me.")
            retryText: qsTr("Try again")
            showRetry: true
            Layout.fillWidth: true
            onRetryRequested: {
                page.elevationDeclined = false;
                page.installRequested();
            }

            // M11: errors arrive, they don't bounce — a single fade in.
            onVisibleChanged: {
                if (elevationBanner.visible && !Tokens.reducedMotion)
                    bannerFade.restart();
            }
            NumberAnimation on opacity {
                id: bannerFade
                running: false
                from: 0
                to: 1
                duration: Tokens.durNormal
                easing.type: Easing.OutQuad
            }
        }

        Item { Layout.fillHeight: true }

        Kit.Callout {
            tone: Kit.Callout.Info
            text: qsTr("No service, no autostart, no update agent. Dish runs only when you start it, and Installed apps removes it cleanly.")
            Layout.fillWidth: true
        }
    }
}
