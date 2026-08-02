// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The two TERMINAL blockers, and only those two: the host stopped answering, or
// the pad was pulled. The page really is unusable while the path is gone, so it
// says so and offers the one action that helps.
//
// An unsteady link is NOT here. A flapping link would raise and dismiss a modal
// repeatedly and trap the user in a dialog they cannot outrun; it is an inline
// ErrorBanner above the editor instead.
//
// No retry countdown, ever: reconnect backoff exists in the session state
// machine but is not wired to the live connection manager, so any number this
// sheet printed would be fiction.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

ContentDialog {
    id: blocker

    enum Kind {
        ConnectionLost,
        ControllerUnplugged
    }

    property int kind: BlockerDialog.ConnectionLost
    // Names the host in the connection-lost body; ignored by the other kind.
    property string hostName: ""

    signal reconnectRequested()

    readonly property bool hostLost: blocker.kind === BlockerDialog.ConnectionLost

    heading: blocker.hostLost ? qsTr("Connection lost") : qsTr("Controller unplugged")

    rejectText: qsTr("Cancel")
    // Only the host-lost blocker has an action; an unplugged pad is fixed with
    // a hand, not a button.
    acceptText: blocker.hostLost ? qsTr("Reconnect") : ""

    onAccepted: blocker.reconnectRequested()

    body: [
        RowLayout {
            spacing: Tokens.s5
            Layout.fillWidth: true

            // Colour is never the only channel, but the dot travels with the
            // sentence beside it, which is the channel that carries the meaning.
            StatusDot {
                token: blocker.hostLost ? "error" : "warning"
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: Tokens.s2
            }

            Label {
                text: blocker.hostLost
                      ? qsTr("%1 stopped responding. Input isn’t getting through. It may be asleep or off your network.").arg(blocker.hostName)
                      : qsTr("Holding the binding so you can reconnect. Re-plug to keep configuring, or close this screen.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    ]
}
