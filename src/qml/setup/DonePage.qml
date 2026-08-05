// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 5: both ends of the install, one page — result as state, never a
// different window. The success face is calm (motion is over; the hero rail
// already settled); the failure face is a diagnosis plus a next step: the
// ErrorCode switch below is the single place an installer error becomes a
// sentence, the log row opens the full story, and Try again re-arms the
// options step.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    readonly property bool failed: Setup.phase === Setup.Failed

    readonly property string hint: ""
    readonly property string primaryLabel: page.failed ? qsTr("Try again") : qsTr("Finish")
    readonly property bool canAdvance: true

    signal retryRequested()
    signal finishRequested()

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function primaryActivated() {
        if (page.failed)
            page.retryRequested();
        else
            page.finishRequested();
        return false;
    }

    readonly property string shortcutsValue: {
        const places = [];
        if (Setup.wantStartMenu)
            places.push(qsTr("Start Menu"));
        if (Setup.wantDesktop)
            places.push(qsTr("Desktop"));
        return places.length > 0 ? places.join(" · ") : qsTr("none");
    }

    // The one ErrorCode -> sentence switch (house render-keys doctrine).
    readonly property string errorText: {
        if (Setup.lastError === Setup.FileOpFailed) {
            return Setup.lastErrorPath.length > 0
                ? qsTr("Couldn’t write to %1 — is another installer running?").arg(Setup.lastErrorPath)
                : qsTr("Something went wrong — see the log for details.");
        }
        if (Setup.lastError === Setup.RegistryFailed)
            return qsTr("Couldn’t register the install with Windows.");
        if (Setup.lastError === Setup.ShortcutFailed) {
            const place = Setup.lastErrorPath.indexOf("Desktop") >= 0 ? qsTr("Desktop")
                                                                     : qsTr("Start Menu");
            return qsTr("Couldn’t create the %1 shortcut.").arg(place);
        }
        if (Setup.lastError === Setup.DiskFull)
            return qsTr("Not enough free space on this drive.");
        if (Setup.lastError === Setup.AppRunning)
            return qsTr("Dish is still running — close it and try again.");
        if (Setup.lastError === Setup.NeedElevation)
            return qsTr("Windows didn’t approve the install. Try again, or go back and pick Just me.");
        if (Setup.lastError === Setup.PayloadCorrupt)
            return qsTr("This installer’s files are damaged. Download it again, then retry.");
        if (Setup.lastError === Setup.RollbackIncomplete)
            return qsTr("The install failed and some files could not be removed — see the log.");
        return qsTr("Something went wrong — see the log for details.");
    }

    component FactRow: RowLayout {
        id: factRow

        property string label: ""
        property string value: ""
        property bool mono: false

        spacing: Tokens.s6

        Accessible.role: Accessible.StaticText
        Accessible.name: factRow.label + " — " + factRow.value

        Kit.Eyebrow {
            text: factRow.label
            mutedTone: true
            Layout.alignment: Qt.AlignVCenter
        }
        // Fills the rest of the row and right-aligns inside it: no spacer, and
        // no width cap reading the width the layout is still deciding (that one
        // made Qt report a recursive rearrange wherever a value was empty).
        Text {
            text: factRow.value
            color: Theme.onSurface
            font.pixelSize: Tokens.textSummary
            font.family: factRow.mono ? Tokens.monoFamily : Tokens.sansFamily
            elide: Text.ElideMiddle
            horizontalAlignment: Text.AlignRight
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }
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
                text: page.failed ? qsTr("Not installed") : qsTr("Installed")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: page.failed ? qsTr("The install did not finish.")
                                  : qsTr("Dish %1 is ready on this PC.").arg(Setup.appVersion)
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // ── Success face ────────────────────────────────────────────────────
        Kit.Card {
            visible: !page.failed
            Layout.fillWidth: true

            contentItem: ColumnLayout {
                spacing: Tokens.s3

                FactRow {
                    label: qsTr("Folder")
                    value: Setup.installDir
                    mono: true
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Shortcuts")
                    value: page.shortcutsValue
                    Layout.fillWidth: true
                }
                FactRow {
                    label: qsTr("Size")
                    value: Setup.requiredText
                    mono: true
                    Layout.fillWidth: true
                }
            }
        }

        Kit.LabeledSwitch {
            visible: !page.failed
            label: qsTr("Start Dish now")
            description: qsTr("Opens straight into first-run setup.")
            checked: Setup.wantLaunch
            onToggled: function (checked) { Setup.wantLaunch = checked; }
            Layout.fillWidth: true
        }

        // ── Failure face ────────────────────────────────────────────────────
        Kit.ErrorBanner {
            id: failureBanner
            visible: page.failed
            text: page.errorText
            detail: Setup.lastErrorPath.length > 0 && Setup.lastError !== Setup.FileOpFailed
                    ? Setup.lastErrorPath : ""
            Layout.fillWidth: true

            // M11: errors arrive, they don't bounce — a single fade in.
            onVisibleChanged: {
                if (failureBanner.visible && !Tokens.reducedMotion)
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

        Kit.RowButton {
            visible: page.failed
            title: qsTr("Open log")
            subtitle: Setup.logFilePath
            Layout.fillWidth: true
            onClicked: Setup.openLogFile()
        }

        Item { Layout.fillHeight: true }

        Kit.Callout {
            visible: !page.failed
            tone: Kit.Callout.Info
            text: qsTr("Remove it any time from Windows Settings ▸ Installed apps.")
            Layout.fillWidth: true
        }
    }
}
