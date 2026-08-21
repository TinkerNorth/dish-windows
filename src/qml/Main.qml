// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The entry window: frameless, with the C++ chrome filter supplying
// snap/resize and WindowTitleBar bleeding into the body so bar and content
// share one surface. It owns the two close policies the shell cannot see — the
// keep-awake confirm and the wizard leave guard — and both run before the
// window may go.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Dish.Chrome
import "kit" as Kit

ApplicationWindow {
    id: root
    width: 980
    height: 640
    // The wizard is the tightest surface in the app: two 232px banner slots, a
    // >=60px wire, the rail and the page padding. Below this it clips.
    minimumWidth: Tokens.minWindowWidth
    minimumHeight: Tokens.minWindowHeight
    visible: true
    title: qsTr("Dish")

    // The C++ FramelessWindowChrome filter restores the native snap/resize/
    // shadow this flag strips. The button HINTS are load-bearing, not
    // decoration: Qt maps them onto Win32 style bits, and FramelessWindowHint
    // alone leaves a bare WS_POPUP with no WS_MAXIMIZEBOX — without that bit
    // Windows treats the window as unmaximizable at all (caption double-click
    // dead, no system-menu Maximize, no Snap Layouts over the HTMAXBUTTON
    // region). They draw nothing; WM_NCCALCSIZE still zeroes the non-client
    // area. Necessary but not sufficient for the maximize BUTTON: that region
    // is non-client, so FramelessWindowChrome runs the press itself.
    flags: Qt.Window | Qt.FramelessWindowHint | Qt.WindowSystemMenuHint
           | Qt.WindowMinimizeButtonHint | Qt.WindowMaximizeButtonHint
           | Qt.WindowCloseButtonHint

    // ALWAYS the themed solid, never a transparent Mica body: the backdrop
    // composed to near-black and drifted with the wallpaper. The chrome filter
    // still extends the frame for the native shadow and snap.
    color: Theme.background

    // Set once both guards have cleared, so the re-entrant close() the guard
    // callback issues passes straight through.
    property bool closeApproved: false

    function approveClose() {
        root.closeApproved = true;
        // Deferred: close() is being called from inside the closing handler.
        Qt.callLater(function () { root.close(); });
    }

    // Windows sends no broadcast a Quick app can bind to for the "animate
    // controls inside windows" preference; re-sample whenever we regain focus.
    onActiveChanged: {
        if (root.active)
            Tokens.refreshMotionPreference();
    }

    // Closing is an intent, not a fact: a page holding an unsaved draft gets
    // first refusal, and an active stream is confirmed rather than dropped.
    onClosing: function (close) {
        if (root.closeApproved)
            return;
        close.accepted = false;
        shell.requestNavigation(function () {
            // Gated on the stream, not on the keep-awake hold: turning keep-awake
            // off must not also remove the confirm before a live stream dies.
            if (App.streamingSlotCount > 0)
                quitConfirm.open();
            else
                root.approveClose();
        });
    }

    WindowTitleBar {
        id: titleBar
        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    // Transparent, so the window's themed body shows through. A first-run flow
    // is pushed here full-screen — over, not inside, the nav shell.
    StackView {
        id: appRoot
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom
        background: null

        initialItem: AppShell { id: shell }
    }

    Kit.ConfirmDialog {
        id: quitConfirm
        eyebrow: qsTr("Streaming")
        heading: qsTr("Stop streaming and quit?")
        bodyText: App.keepAwakeReach === "display"
                  ? qsTr("A controller is still streaming, and the display is being kept awake.")
                  : App.keepAwakeReach === "system"
                    ? qsTr("A controller is still streaming, and the computer is being kept awake.")
                    : qsTr("A controller is still streaming.")
        acceptText: qsTr("Quit")
        rejectText: qsTr("Cancel")
        destructiveAccept: true
        onAccepted: {
            quitConfirm.close();
            root.approveClose();
        }
    }

    // Skip is a completion too, or the welcome loops forever.
    Component.onCompleted: {
        if (App.onboardingNeeded) {
            const flow = appRoot.push(Qt.resolvedUrl("onboarding/OnboardingFlow.qml"));
            flow.completed.connect(function (runSetup) {
                appRoot.pop();
                App.markOnboardingComplete();
                if (runSetup)
                    shell.openSetupWizard("");
            });
        }
    }
}
