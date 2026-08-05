// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The running-app gate. Accept asks Dish to quit (WM_CLOSE + grace) and the
// dialog STAYS OPEN while the engine waits; only after that graceful attempt
// fails does the accept swap to the destructive Force close — terminate is
// never the first offer. Retry rescans; reject (or Esc) cancels the
// operation, and the onClosed guard makes Esc equivalent to reject so the
// engine is never left waiting behind a dismissed dialog.
//
// The body's counted line is THE one %n string in the installer (spec D18):
// Bosnian needs its three plural forms and English both, filled in the .ts
// catalogues.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Kit.ContentDialog {
    id: dialog

    // Flavours the diagnosis line: an install replaces files, an uninstall
    // removes them.
    property bool uninstallMode: false

    // Set once the graceful close has been asked of the engine; while the
    // blockers survive it, the dialog escalates.
    property bool closeAttempted: false
    readonly property bool graceFailed: dialog.closeAttempted && Setup.appRunning

    eyebrow: qsTr("In the way")
    heading: qsTr("Dish is running")
    acceptText: dialog.graceFailed ? qsTr("Force close") : qsTr("Close Dish and continue")
    rejectText: qsTr("Cancel")
    destructiveAccept: dialog.graceFailed

    onOpened: {
        dialog.closeAttempted = false;
        // Reject holds the default focus so a stray Enter is never the
        // destructive answer (ConfirmDialog's rule, replicated).
        const first = dialog.contentItem ? dialog.contentItem.nextItemInFocusChain(true) : null;
        if (first)
            first.forceActiveFocus(Qt.TabFocusReason);
    }

    onAccepted: {
        // Stays open either way: the host closes it when the blockers are
        // gone (phase leaves AwaitingBlockers).
        if (dialog.graceFailed) {
            Setup.resolveBlockers(true);
        } else {
            dialog.closeAttempted = true;
            Setup.resolveBlockers(false);
        }
    }

    onClosed: {
        // Esc and reject both land here. If the engine is still waiting on
        // the gate, a dismissed dialog IS a cancel — never a silent hang.
        if (Setup.phase === Setup.AwaitingBlockers)
            Setup.cancel();
    }

    body: [
        Label {
            // THE counted string (D18): the one %n surface in the installer.
            text: qsTr("%n running Dish window(s) must close before Setup continues.", "",
                       Setup.runningProcessCount)
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        Label {
            text: dialog.graceFailed
                  ? qsTr("Dish didn’t close. Save anything in flight, close it yourself, then try again.")
                  : dialog.uninstallMode
                    ? qsTr("Close it to continue — files it holds open can’t be removed.")
                    : qsTr("Close it to continue — replacing files under a running app breaks it.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        Label {
            // The processes themselves: data, not prose — shown verbatim.
            visible: Setup.runningProcessNames.length > 0
            text: Setup.runningProcessNames.join(" · ")
            color: Theme.mutedStrong
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textChip
            elide: Text.ElideMiddle
            Layout.fillWidth: true
        },
        Kit.DishButton {
            visible: dialog.graceFailed
            text: qsTr("Try again")
            variant: Kit.DishButton.Outline
            size: Kit.DishButton.Small
            onClicked: Setup.rescanBlockers()
        }
    ]
}
