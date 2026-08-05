// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Uninstall step 2: the Installing page's skeleton run backwards — the hero
// beam crawls upward and carries no packets (removal has no payload).
// Removal is not resumable, so there is no cancel once it starts. The
// self-delete step is drawn honestly as the one thing that finishes after
// this window closes ("on exit" — the helper's job), which is also why it
// never claims done here.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    // Whether this run ever hit the running-app gate.
    property bool hadBlockers: false

    readonly property bool failed: Setup.phase === Setup.Failed

    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Remove") // frozen while removing
    readonly property bool canAdvance: false

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    function shortDir(path) {
        if (!path || path.length === 0)
            return "";
        const parts = path.split(/[\\/]+/).filter(function (s) { return s.length > 0; });
        if (parts.length <= 2)
            return path;
        return "…\\" + parts[parts.length - 2] + "\\" + parts[parts.length - 1];
    }

    readonly property string failedRow: {
        if (Setup.lastError === Setup.AppRunning)
            return "stop";
        if (Setup.lastError === Setup.ShortcutFailed)
            return "shortcuts";
        if (Setup.lastError === Setup.RegistryFailed)
            return "unregister";
        return "files";
    }

    readonly property string errorText: {
        if (Setup.lastError === Setup.FileOpFailed && Setup.lastErrorPath.length > 0)
            return qsTr("Couldn’t remove %1 — is it open somewhere?").arg(Setup.lastErrorPath);
        if (Setup.lastError === Setup.RegistryFailed)
            return qsTr("Couldn’t unregister the install from Windows.");
        if (Setup.lastError === Setup.RollbackIncomplete)
            return qsTr("Some files could not be removed — see the log.");
        return qsTr("Something went wrong — see the log for details.");
    }

    readonly property var steps: {
        const ph = Setup.phase;
        const failing = ph === Setup.RollingBack || ph === Setup.Failed;
        const started = ph === Setup.Copying || ph === Setup.Committing
                        || ph === Setup.Finalizing || ph === Setup.Done;
        const rows = [];
        if (page.hadBlockers) {
            rows.push({
                "label": qsTr("Stop the running app"),
                "meta": qsTr("waiting"),
                "state": failing && page.failedRow === "stop" ? "failed" : "done"
            });
        }
        if (Setup.wantStartMenu || Setup.wantDesktop) {
            const places = [];
            if (Setup.wantStartMenu)
                places.push(qsTr("start menu"));
            if (Setup.wantDesktop)
                places.push(qsTr("desktop"));
            rows.push({
                "label": qsTr("Shortcuts"),
                "meta": places.join(" · "),
                "state": failing && page.failedRow === "shortcuts" ? "failed"
                       : started ? "done" : "pending"
            });
        }
        rows.push({
            "label": qsTr("Remove files"),
            "meta": Setup.requiredText,
            "state": failing && page.failedRow === "files" ? "failed"
                   : (ph === Setup.Finalizing || ph === Setup.Done) ? "done"
                   : (ph === Setup.Copying || ph === Setup.Committing) ? "active"
                   : "pending"
        });
        rows.push({
            "label": qsTr("Unregister from Windows"),
            "meta": qsTr("installed apps"),
            "state": failing && page.failedRow === "unregister" ? "failed"
                   : ph === Setup.Done ? "done"
                   : ph === Setup.Finalizing ? "active"
                   : "pending"
        });
        rows.push({
            "label": qsTr("Clean up this uninstaller"),
            "meta": qsTr("on exit"),
            "state": "pending" // the helper finishes this after the window closes
        });
        return rows;
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
                text: qsTr("Removing…")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Taking Dish out of %1.").arg(page.shortDir(Setup.existingDir))
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Kit.DishProgressBar {
            indeterminate: false
            value: Setup.progress
            Accessible.role: Accessible.ProgressBar
            Accessible.name: qsTr("Removal progress")
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Text {
                // Manifest paths are '/'-separated; the surrounding copy draws
                // Windows paths, so this line does too.
                text: Setup.currentFile.replace(/\//g, "\\")
                color: Theme.mutedStrong
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
                elide: Text.ElideLeft
                Layout.fillWidth: true
            }
            Text {
                text: qsTr("file %1 of %2").arg(Setup.fileIndex).arg(Setup.fileCount)
                color: Theme.mutedStrong
                font.family: Tokens.monoFamily
                font.pixelSize: Tokens.textChip
            }
        }

        Kit.ErrorBanner {
            id: failureBanner
            visible: page.failed
            text: page.errorText
            detail: Setup.lastError !== Setup.FileOpFailed && Setup.lastErrorPath.length > 0
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

        Kit.StepList {
            steps: page.steps
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
