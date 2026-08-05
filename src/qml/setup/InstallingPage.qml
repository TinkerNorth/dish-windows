// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 4: the transmission. A page, not an overlay — there is nothing behind
// to return to, so the StepList lives on the page and the hero rail carries
// the drama. Progress is byte-accurate from manifest sizes; the telemetry
// line is data, not prose (the file path is shown verbatim, mono, elided
// LEFT because tails matter). Cancel lives only while copying: the commit
// and finalize windows are sub-second and atomic. Failure never stays here —
// the host advances to the Done page's failure face.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    // Whether this run ever hit the running-app gate; the row only exists if
    // it did (a step that never had to run is noise, not honesty).
    property bool hadBlockers: false

    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Install") // frozen while applying
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

    // Which StepList row the recorded error belongs to. lastError can still be
    // NoError while the rollback replays; the copy row is the only step whose
    // failure is not separately typed, so it takes the blame.
    readonly property string failedRow: {
        if (Setup.lastError === Setup.AppRunning)
            return "stop";
        if (Setup.lastError === Setup.ShortcutFailed)
            return "shortcuts";
        if (Setup.lastError === Setup.RegistryFailed)
            return "register";
        return "copy";
    }

    readonly property var steps: {
        const ph = Setup.phase;
        const failing = ph === Setup.RollingBack || ph === Setup.Failed;
        const rows = [];
        if (page.hadBlockers) {
            rows.push({
                "label": qsTr("Stop the running app"),
                "meta": qsTr("waiting"),
                "state": failing && page.failedRow === "stop" ? "failed" : "done"
            });
        }
        rows.push({
            "label": qsTr("Copy files"),
            "meta": Setup.requiredText,
            "state": failing && page.failedRow === "copy" ? "failed"
                   : (ph === Setup.Finalizing || ph === Setup.Done) ? "done"
                   : (ph === Setup.Copying || ph === Setup.Committing) ? "active"
                   : "pending"
        });
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
                       : (ph === Setup.Finalizing || ph === Setup.Done) ? "done"
                       : "pending"
            });
        }
        rows.push({
            "label": qsTr("Register with Windows"),
            "meta": qsTr("installed apps"),
            "state": failing && page.failedRow === "register" ? "failed"
                   : ph === Setup.Done ? "done"
                   : ph === Setup.Finalizing ? "active"
                   : "pending"
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
                text: qsTr("Installing…")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Copying Dish into %1.").arg(page.shortDir(Setup.installDir))
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
            Accessible.name: qsTr("Install progress")
            Layout.fillWidth: true
        }

        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            Text {
                // Manifest paths are stored '/'-separated; this line sits under
                // a sub-line and beside a rail that both draw Windows paths, so
                // it says the same thing they do.
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

        Kit.Callout {
            visible: Setup.phase === Setup.RollingBack
            tone: Kit.Callout.Warning
            text: qsTr("Undoing changes…")
            Layout.fillWidth: true
        }

        Kit.StepList {
            steps: page.steps
            Layout.fillWidth: true
        }

        Item { Layout.fillHeight: true }
    }
}
