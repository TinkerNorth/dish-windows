// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The inline failure row — the shared "this failed, here is what to do" surface
// so pages stop hand-rolling a bare `Label { color: Theme.error }`. A tone-tinted
// rounded rect with a leading dot, the diagnosis in Theme.onSurface, an optional
// `detail` next-step line, and an optional Retry that emits `retryRequested()`.
//
// An error is a DIAGNOSIS AND A NEXT STEP, never an apology — hence two strings:
//   text:   "Server unreachable"
//   detail: "Has it moved networks?"
//
// Two documented callers: the inline unsteady-link banner on Configure binding
// (tone: Warning — a faltering link is not a failure, and a modal on a flapping
// link is a trap) and the catalog-retry row in the wizard's type step.
//
// Usage:
//   Kit.ErrorBanner {
//       text: lastError
//       detail: qsTr("Check the host is on the same network.")
//       showRetry: true
//       onRetryRequested: App.reconnectConnection(connectionId)
//   }

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Rectangle {
    id: banner

    enum Tone { Error, Warning }

    // The failure message (the diagnosis).
    property string text: ""
    // The next step. Errors that name no next step are not finished.
    property string detail: ""
    // Error is a failure; Warning is "live but faltering".
    property int tone: ErrorBanner.Error
    // The retry button's label.
    property string retryText: qsTr("Retry")
    // Whether to show the Retry button at all.
    property bool showRetry: false

    // Emitted when the user taps Retry — the page decides what to re-run.
    signal retryRequested()

    readonly property color toneColor: banner.tone === ErrorBanner.Warning
                                       ? Theme.warning : Theme.error

    // Collapse to nothing when there's no message to show.
    visible: banner.text.length > 0

    implicitHeight: layout.implicitHeight + 2 * Tokens.s6
    implicitWidth: layout.implicitWidth + 2 * Tokens.s7
    radius: Tokens.radiusCard
    color: banner.tone === ErrorBanner.Warning ? Theme.warningFill : Theme.errorFill
    border.width: 1
    border.color: Theme.alpha(banner.toneColor, 0.35)

    Accessible.role: Accessible.AlertMessage
    Accessible.name: banner.text
    Accessible.description: banner.detail

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: Tokens.s7
        anchors.rightMargin: Tokens.s7
        anchors.topMargin: Tokens.s6
        anchors.bottomMargin: Tokens.s6
        spacing: Tokens.s5

        // Leading tone dot — the same dot affordance the kit uses for status.
        StatusDot {
            token: banner.tone === ErrorBanner.Warning ? "warning" : "error"
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: Tokens.s2
        }

        ColumnLayout {
            spacing: Tokens.s1
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter

            Label {
                text: banner.text
                color: Theme.onSurface
                font.pixelSize: Tokens.textBase
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Label {
                text: banner.detail
                visible: banner.detail.length > 0
                color: Theme.mutedStrong
                font.pixelSize: Tokens.textMeta
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        // Quiet outline action so the message keeps the visual weight; emits the
        // signal rather than acting, so the page owns the retry behavior.
        DishButton {
            text: banner.retryText
            visible: banner.showRetry
            variant: DishButton.Outline
            size: DishButton.Small
            Layout.alignment: Qt.AlignVCenter
            onClicked: banner.retryRequested()
        }
    }
}
