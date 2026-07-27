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

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Dish.Chrome

ApplicationWindow {
    id: root
    width: 980
    height: 640
    minimumWidth: 560
    minimumHeight: 380
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

    // First-run gate (C++/Main owns the policy, per the onboarding convention).
    // If the onboarding store reports the welcome flow hasn't completed, push the
    // flow full-screen over the shell; its one-shot completed(runSetup) pops it,
    // marks onboarding done via the App surface, and — when the final page asked
    // for it — opens the setup guide dialog over the freshly-revealed shell.
    Component.onCompleted: {
        if (App.onboardingNeeded) {
            const flow = appRoot.push(Qt.resolvedUrl("onboarding/OnboardingFlow.qml"));
            flow.completed.connect(function (runSetup) {
                appRoot.pop();
                App.markOnboardingComplete();
                if (runSetup)
                    shell.openSetupGuide();
            });
        }
    }
}
