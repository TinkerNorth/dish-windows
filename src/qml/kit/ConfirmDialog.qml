// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The one confirm sheet in the app. Four callers — the wizard's discard
// confirm, Forget host, Forget controller, and the keep-awake close confirm —
// so there is exactly one shape for "are you sure", and the destructive
// treatment is a property rather than an inline colour.
//
// `bulletLines` is the part that matters: a Forget must NAME what it drops, one
// item per line. A confirm that says "some bindings will be lost" is not a
// confirm.
//
// Reject holds the default focus: these sheets are reached by keyboard as often
// as by mouse, and a stray Enter must never be the destructive answer.

// Bound: the bullet delegate resolves against the outer scope.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

ContentDialog {
    id: confirm

    // The one-sentence consequence, above the bullets.
    property string bodyText: ""
    // What will actually be dropped/deleted — pad names, binding names.
    property var bulletLines: []

    // acceptText / rejectText / acceptEnabled / destructiveAccept / eyebrow /
    // heading are all inherited from ContentDialog.

    // The sheet takes keyboard focus so the footer buttons are reachable
    // without a mouse; onOpened then parks it on reject.
    focus: true

    onOpened: {
        // Reject is the default: the first focusable item inside the sheet is
        // the reject button (the body carries labels only), so seeding the
        // chain from the content root lands on it.
        const first = confirm.contentItem ? confirm.contentItem.nextItemInFocusChain(true) : null;
        if (first)
            first.forceActiveFocus(Qt.TabFocusReason);
    }

    body: [
        Label {
            visible: confirm.bodyText.length > 0
            text: confirm.bodyText
            color: Theme.muted
            font.pixelSize: Tokens.textSummary
            lineHeight: 1.5
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        Column {
            visible: confirm.bulletLines.length > 0
            spacing: Tokens.s1
            Layout.fillWidth: true

            Repeater {
                model: confirm.bulletLines

                delegate: Row {
                    id: bullet
                    required property string modelData
                    spacing: Tokens.s3

                    Text {
                        text: "·"
                        color: Theme.mutedStrong
                        font.pixelSize: Tokens.textBase
                    }
                    Text {
                        text: bullet.modelData
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textSummary
                    }
                }
            }
        }
    ]
}
