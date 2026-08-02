// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The transient toast / snackbar host — drop it ONCE at the app shell so a
// one-shot App.errorMessage (and any other transient notice) has somewhere to
// land. A bottom-center stack of rail-accented pills (Theme.surface fill,
// Theme.outline border, a tone-coloured leading rule), each fading + sliding in,
// sitting for Tokens.durToast, then fading out. Safe to call show() repeatedly —
// each call pushes a new toast with its own auto-dismiss timer.
//
// TONES ARE error | warning | success. There is no `info` tone: persistent state
// lives on the surface that owns it, so an informational toast is by
// construction state that has escaped its surface. A stray "info" maps to
// success and warns.
//
// This is the ONE elevated surface in the app — it is the only thing that floats
// without a scrim, so the shadow is what separates it from the page. Dialogs
// deliberately have none.
//
// API:
//   show(message)                       // severity defaults to "error"
//   show(message, severity)             // severity ∈ "error" | "warning" | "success"
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
import QtQuick.Effects
import QtQuick.Layouts
import Dish.Chrome

Item {
    id: host

    // How long (ms) a toast stays before it auto-dismisses.
    property int durationMs: Tokens.durToast
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

    // Push a toast. `severity` is one of "error" | "warning" | "success" and
    // defaults to "error" (the App.errorMessage channel). Safe to call any number
    // of times; each call appends a row with its own auto-dismiss timer (declared
    // on the delegate). An empty message is ignored.
    function show(message, severity) {
        if (!message || message.length === 0)
            return;
        let sev = severity;
        if (sev === "info") {
            console.warn("NotificationToastHost: the 'info' tone was removed — "
                         + "persistent state belongs on its own surface. Mapped to "
                         + "'success': " + message);
            sev = "success";
        }
        if (sev !== "warning" && sev !== "success")
            sev = "error";
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
        anchors.bottomMargin: Tokens.s8
        spacing: Tokens.s4

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

                // Severity → the leading rule colour. Three tones, no info.
                readonly property color accent: toast.severity === "success" ? Theme.success
                                              : toast.severity === "warning" ? Theme.warning
                                              : Theme.error

                Accessible.role: Accessible.AlertMessage
                Accessible.name: toast.message

                Rectangle {
                    id: pill
                    // The ONE elevated Dish surface: a surface pill with a 3px
                    // tone rule down the left edge and a soft shadow.
                    implicitWidth: Math.min(pillRow.implicitWidth + 28, 380)
                    implicitHeight: Math.max(pillRow.implicitHeight + 24, 40)
                    radius: Tokens.radiusButton
                    color: Theme.surface
                    border.width: 1
                    border.color: Theme.outline
                    layer.enabled: true
                    layer.effect: MultiEffect {
                        shadowEnabled: true
                        shadowBlur: 0.8
                        shadowVerticalOffset: 8
                        shadowColor: Qt.rgba(0, 0, 0, 0.45)
                    }

                    // Leading severity rule down the left edge (squared — it
                    // reads as a rule, not a bar).
                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        width: 3
                        color: toast.accent
                    }

                    RowLayout {
                        id: pillRow
                        anchors.fill: parent
                        anchors.leftMargin: Tokens.s7
                        anchors.rightMargin: Tokens.s6
                        anchors.topMargin: Tokens.s6
                        anchors.bottomMargin: Tokens.s6
                        spacing: Tokens.s5

                        Label {
                            text: toast.message
                            color: Theme.onSurface
                            font.pixelSize: Tokens.textSummary
                            lineHeight: 1.45
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                            Layout.maximumWidth: 320
                            Layout.alignment: Qt.AlignVCenter
                        }

                        // Explicit dismiss affordance, drawn as a vector: the
                        // design bans ✕ as text (no reliable Windows glyph).
                        Canvas {
                            id: dismissMark
                            implicitWidth: Tokens.s5
                            implicitHeight: Tokens.s5
                            Layout.alignment: Qt.AlignTop
                            onPaint: {
                                var ctx = getContext("2d");
                                ctx.reset();
                                // The whole pill is the dismiss target, so the
                                // mark brightens with the pill, not on its own.
                                var c = pillMouse.containsMouse ? Theme.onSurface : Theme.muted;
                                ctx.strokeStyle = Qt.rgba(c.r, c.g, c.b, c.a);
                                ctx.lineWidth = 1.4;
                                ctx.beginPath();
                                ctx.moveTo(1.5, 1.5);
                                ctx.lineTo(width - 1.5, height - 1.5);
                                ctx.moveTo(width - 1.5, 1.5);
                                ctx.lineTo(1.5, height - 1.5);
                                ctx.stroke();
                            }
                            Connections {
                                target: Theme
                                function onPaletteChanged() { dismissMark.requestPaint(); }
                            }
                            Connections {
                                target: pillMouse
                                function onContainsMouseChanged() { dismissMark.requestPaint(); }
                            }
                        }
                    }

                    // Tap anywhere on the toast to dismiss it early. A toast is
                    // never in the tab order: it announces itself as an
                    // AlertMessage and disappears on its own, so taking keyboard
                    // focus would interrupt whatever the user is actually doing.
                    MouseArea {
                        id: pillMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: host._dismiss(toast.toastId)
                    }
                }

                // ── Enter / exit motion ──────────────────────────────────────
                // Fade + slide up on appear; the auto-dismiss is a one-shot Timer
                // started on completion. (No exit slide: removing the model row
                // destroys the delegate, so we keep the appear motion and let the
                // ListModel removal carry the disappearance.)
                opacity: 0
                transform: Translate { id: slide; y: 12 }

                NumberAnimation {
                    id: slideIn
                    target: slide
                    property: "y"
                    from: 12
                    to: 0
                    duration: Tokens.reducedMotion ? 0 : Tokens.durFast
                    easing.type: Easing.OutCubic
                }
                NumberAnimation {
                    id: fadeIn
                    target: toast
                    property: "opacity"
                    from: 0
                    to: 1
                    duration: Tokens.reducedMotion ? 0 : Tokens.durFast
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
