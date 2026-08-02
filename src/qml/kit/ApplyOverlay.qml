// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The app-opened apply sheet: a ContentDialog wrapping a StepList, with an
// optional escape. Two callers — the wizard's Review page and Configure
// binding — so the copy, the width and the dismiss rules live here once.
//
// It is deliberately NOT dismissable by default: the 8s REST round-trip is
// short enough not to need one. `cancellable` is raised only while the
// Connection step is active, where a Direct claim can sit for 20s waiting on
// Windows to release the device; aborting that falls back to Standard, which
// is a warning, not a failure. `slowHint` carries the 4s explanation.
//
// The caller owns the steps and the outcome: this sheet never closes itself on
// success, and never posts a toast.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

ContentDialog {
    id: overlay

    // -> StepList.steps: [{ label, meta, state }]
    property var steps: []
    // True only while the step that can be aborted is the active one.
    property bool cancellable: false
    // Shown under the steps once the active step has outstayed its budget.
    property string slowHint: ""

    signal cancelRequested()

    eyebrow: qsTr("Applying")
    heading: qsTr("Applying binding…")
    preferredWidth: 430

    // A report, not a question: there is no accept. The reject slot becomes
    // the escape, and only while the caller says the step can be aborted.
    acceptText: ""
    rejectText: overlay.cancellable ? qsTr("Cancel") : ""
    closePolicy: overlay.cancellable ? Popup.CloseOnEscape : Popup.NoAutoClose

    onRejected: overlay.cancelRequested()

    body: [
        Label {
            text: qsTr("Sending each setting to the host.")
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        StepList {
            steps: overlay.steps
            Layout.fillWidth: true
            Layout.topMargin: Tokens.s1
        },
        Label {
            visible: overlay.slowHint.length > 0
            text: overlay.slowHint
            color: Theme.mutedStrong
            font.pixelSize: Tokens.textMeta
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    ]
}
