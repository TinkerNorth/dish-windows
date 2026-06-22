// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The transient toast / snackbar host — drop it ONCE at the app shell so a
// one-shot App.errorMessage (and any other transient notice) has somewhere to
// land. Mirrors the Widgets NotificationToastHost: a bottom-center stack of
// rail-accented pills (Theme.surface fill, Theme.outline border, a
// severity-coloured leading bar), each fading + sliding in, sitting for ~4 s,
// then fading out. Safe to call show() repeatedly — each call pushes a new toast
// onto the stack with its own auto-dismiss timer.
//
// API:
//   show(message)                       // severity defaults to "error"
//   show(message, severity)             // severity ∈ "error" | "info" | "success"
//
// Wiring (in AppShell.qml / Main.qml — declare one and connect App.errorMessage):
//   Kit.NotificationToastHost { id: toastHost }
//   Connections {
//       target: App
//       function onErrorMessage(m) { toastHost.show(m) }   // m: string
//   }
//
// It reparents nothing and paints nothing of its own beyond the toasts, so it is
// safe to place as a sibling of the shell's content (give it the same anchors as
// the content area, or anchors.fill of the window body) — it only draws at the
// bottom-center within its own bounds.

// Bound component behavior so the toast delegate can reference the outer `host`
// id (its show/_dismiss API + tuning props) and its own `required` model roles
// statically — keeps binding resolution static and qmllint quiet (matches
// ConnectionsPage / ControllersPage).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Item {
    id: host

    // How long (ms) a toast stays before it auto-dismisses.
    property int durationMs: 4000
    // Cap the visible stack so a flood of errors can't grow without bound; the
    // oldest is dropped when a new one would exceed this (DROP_OLDEST, matching
    // the source-layer channel policy).
    property int maxVisible: 4

    // Don't eat clicks meant for the content beneath us — only the toast pills
    // themselves are interactive (their own MouseArea). The host fills its parent
    // purely to host the bottom-anchored stack.
    anchors.fill: parent

    // Monotonic id so each toast is individually addressable for dismissal.
    property int _nextId: 1

    // The backing model of live toasts: { toastId, message, severity }. A
    // ListModel + Repeater (not an imperative createObject loop) so the stack is
    // declarative and each entry's lifetime is the model row's lifetime.
    ListModel { id: toastModel }

    // Push a toast. `severity` is one of "error" | "info" | "success" and
    // defaults to "error" (the App.errorMessage channel). Safe to call any number
    // of times; each call appends a row with its own auto-dismiss timer (declared
    // on the delegate). An empty message is ignored.
    function show(message, severity) {
        if (!message || message.length === 0)
            return;
        const sev = (severity === "info" || severity === "success") ? severity : "error";
        // DROP_OLDEST: trim from the front until there's room for the new one.
        while (toastModel.count >= host.maxVisible)
            toastModel.remove(0);
        toastModel.append({ toastId: host._nextId++, message: message, severity: sev });
    }

    // Remove a toast by id (the delegate calls this when its timer fires or the
    // user taps it). Resolved by id, not index, because the index shifts as
    // earlier toasts dismiss.
    function _dismiss(toastId) {
        for (let i = 0; i < toastModel.count; ++i) {
            if (toastModel.get(i).toastId === toastId) {
                toastModel.remove(i);
                return;
            }
        }
    }

    // The stack itself: bottom-centered, newest at the bottom. A Column laid out
    // bottom-up so a new toast pushes in beneath the existing ones.
    Column {
        id: stack
        anchors.bottom: parent.bottom
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottomMargin: 24
        spacing: 8

        Repeater {
            model: toastModel

            delegate: Item {
                id: toast
                required property int index
                required property int toastId
                required property string message
                required property string severity

                // Size to the pill; the pill sizes to its content.
                implicitWidth: pill.implicitWidth
                implicitHeight: pill.implicitHeight
                width: implicitWidth
                height: implicitHeight

                // Severity → accent colour (the leading rail bar). Error/info/
                // success map onto the same Theme tones the rest of the kit uses.
                readonly property color accent: toast.severity === "success" ? Theme.success
                                              : toast.severity === "info" ? Theme.primary
                                              : Theme.error

                Rectangle {
                    id: pill
                    // The pill surface — a Card-like panel so the toast reads as a
                    // floating surface over the Mica/content, with a coloured rail.
                    implicitWidth: Math.min(pillRow.implicitWidth + 28, 420)
                    implicitHeight: Math.max(pillRow.implicitHeight + 20, 40)
                    radius: 10
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.outline

                    // Leading severity rail down the left edge.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 4
                        radius: 2
                        color: toast.accent
                    }

                    RowLayout {
                        id: pillRow
                        anchors.fill: parent
                        anchors.leftMargin: 16
                        anchors.rightMargin: 12
                        anchors.topMargin: 10
                        anchors.bottomMargin: 10
                        spacing: 10

                        // A small severity dot echoes the rail for quick scanning.
                        Rectangle {
                            implicitWidth: 8
                            implicitHeight: 8
                            radius: 4
                            color: toast.accent
                            Layout.alignment: Qt.AlignVCenter
                        }

                        Label {
                            text: toast.message
                            color: Theme.onSurface
                            font.pixelSize: 13
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.maximumWidth: 340
                            Layout.alignment: Qt.AlignVCenter
                        }
                    }

                    // Tap anywhere on the toast to dismiss it early.
                    MouseArea {
                        anchors.fill: parent
                        cursorShape: Qt.PointingHandCursor
                        onClicked: host._dismiss(toast.toastId)
                    }
                }

                // ── Enter / exit motion ──────────────────────────────────────
                // Fade + slide up on appear; the auto-dismiss is a one-shot Timer
                // started on completion. (No exit slide: removing the model row
                // destroys the delegate, so we keep the appear motion and let the
                // ListModel removal carry the disappearance — matching the Widgets
                // host, which leans on opacity for the whole transition.)
                opacity: 0
                transform: Translate { id: slide; y: 12 }

                NumberAnimation {
                    id: slideIn
                    target: slide
                    property: "y"
                    from: 12
                    to: 0
                    duration: 160
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    id: fadeIn
                    target: toast
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: 160
                }

                Timer {
                    id: autoDismiss
                    interval: host.durationMs
                    repeat: false
                    onTriggered: host._dismiss(toast.toastId)
                }

                Component.onCompleted: {
                    slideIn.start();
                    fadeIn.start();
                    autoDismiss.start();
                }
            }
        }
    }
}
