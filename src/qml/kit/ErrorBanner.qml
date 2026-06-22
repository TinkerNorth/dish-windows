// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// An inline error row — the shared "this failed" surface so pages stop
// hand-rolling a bare `Label { color: Theme.error }`. An error-tinted rounded
// rect (Theme.error at low alpha for the fill + a leading error dot), the
// message in Theme.onSurface, and an optional Retry button (shown when
// `showRetry`) that emits `retryRequested()`. Drop it inside a Kit.Card or a
// Kit.Page; it fills the available width.
//
// Usage:
//   Kit.ErrorBanner {
//       text: lastError
//       showRetry: true
//       onRetryRequested: App.reconnectConnection(connectionId)
//   }

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Rectangle {
    id: banner

    // The failure message.
    property string text: ""
    // The retry button's label.
    property string retryText: qsTr("Retry")
    // Whether to show the Retry button at all.
    property bool showRetry: false

    // Emitted when the user taps Retry — the page decides what to re-run.
    signal retryRequested()

    // Collapse to nothing when there's no message to show.
    visible: banner.text.length > 0

    implicitHeight: layout.implicitHeight + 24
    implicitWidth: layout.implicitWidth + 28
    radius: 8
    // Error-tinted fill + a slightly stronger error border (the design-system
    // error wash; matches the alpha-tint recipe used across the kit).
    color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.12)
    border.width: 1
    border.color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.35)

    RowLayout {
        id: layout
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.topMargin: 12
        anchors.bottomMargin: 12
        spacing: 10

        // Leading error dot — the same dot affordance the kit uses for status,
        // here pinned to the error tone.
        Rectangle {
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: Theme.error
            Layout.alignment: Qt.AlignVCenter
        }

        Label {
            text: banner.text
            color: Theme.onSurface
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.alignment: Qt.AlignVCenter
        }

        // Quiet outline action so the message keeps the visual weight; emits the
        // signal rather than acting, so the page owns the retry behavior.
        OutlineButton {
            text: banner.retryText
            visible: banner.showRetry
            implicitHeight: 28
            Layout.alignment: Qt.AlignVCenter
            onClicked: banner.retryRequested()
        }
    }
}
