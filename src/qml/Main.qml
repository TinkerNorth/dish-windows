// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Qt Quick entry window for the modern Windows chrome (DISH_QML build). A
// frameless ApplicationWindow: the OS draws no title bar, the C++ chrome filter
// supplies snap/resize/Mica, and WindowTitleBar bleeds into the body on the same
// surface so there is no seam between the bar and the content. Below the title
// bar a top-level StackView hosts the app: the AppShell (nav rail + content) is
// the root; a first-run onboarding flow is pushed full-screen over it OUTSIDE
// the nav shell (see docs/QML_UI_KIT.md "Onboarding convention").
//
// The window owns two close policies the shell cannot see: the keep-awake
// confirm (we are still streaming and holding the display awake) and the leave
// guard (a wizard draft on the content stack). Both run before the window is
// allowed to go.

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

    // Frameless: we draw our own chrome. The C++ FramelessWindowChrome filter
    // restores the native snap/resize/shadow that this flag otherwise strips.
    flags: Qt.Window | Qt.FramelessWindowHint

    // ALWAYS the themed solid: the flows design paints the deep-space body
    // (#060818 dark / off-white light) — a Mica-transparent window composed to
    // near-BLACK and drifted with the wallpaper, which is not the design's
    // surface. The chrome filter still extends the frame for the native
    // shadow/snap; only the backdrop showing THROUGH the body is retired.
    color: Theme.background

    // Set once a close has cleared both guards, so the re-entrant close() the
    // guard callback issues passes straight through.
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

    // Closing is an intent, not a fact. A page holding an unsaved draft gets
    // first refusal (the wizard raises its discard confirm), and an active
    // stream is confirmed rather than dropped — the app must never instruct the
    // user to compensate for behaviour it does not have.
    onClosing: function (close) {
        if (root.closeApproved)
            return;
        close.accepted = false;
        shell.requestNavigation(function () {
            if (App.keepAwakeActive)
                quitConfirm.open();
            else
                root.approveClose();
        });
    }

    // The title bar bleeds into the body: same parent, no divider. The bar
    // publishes its caption + maximize-button geometry up to C++ for hit-testing.
    WindowTitleBar {
        id: titleBar
        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    // Top-level host below the title bar. Transparent so Mica shows through the
    // whole body. The AppShell (nav shell) is the root item; the onboarding
    // agent shows a first-run flow by pushing a full-screen page here — over,
    // not inside, the nav shell — and pops it to reveal the shell:
    //   appRoot.push("onboarding/OnboardingFlow.qml")   // ... later: appRoot.pop()
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
        bodyText: qsTr("A controller is still streaming and the display is being kept awake.")
        acceptText: qsTr("Quit")
        rejectText: qsTr("Cancel")
        destructiveAccept: true
        onAccepted: {
            quitConfirm.close();
            root.approveClose();
        }
    }

    // First-run gate (C++/Main owns the policy, per the onboarding convention).
    // If the onboarding store reports the welcome flow hasn't completed, push the
    // flow full-screen over the shell; its one-shot completed(runSetup) pops it,
    // marks onboarding done via the App surface — Skip is a completion too, or
    // the welcome loops forever — and, when the final page asked for it, opens
    // the setup wizard on the freshly-revealed shell.
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
