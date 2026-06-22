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

    // When Mica is active the OS backdrop shows through a transparent surface;
    // ChromeBridge.micaActive is set by main.cpp after applyMicaBackdrop(). On
    // pre-Win11 we paint the themed solid background instead.
    // Only stay transparent in DARK mode: Mica's tint follows the OS, so a light
    // app over a dark desktop would keep a dark backdrop while the content goes
    // light ("only some items went light"). Light mode paints the solid bg.
    color: (ChromeBridge.micaActive && ChromeBridge.dark) ? "transparent" : Theme.background

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

        initialItem: AppShell {}
    }

    // First-run gate (C++/Main owns the policy, per the onboarding convention).
    // If the onboarding store reports the welcome flow hasn't completed, push the
    // flow full-screen over the shell; its one-shot completed() pops it and marks
    // onboarding done via the App surface (App.onboardingNeeded then flips false).
    Component.onCompleted: {
        if (App.onboardingNeeded) {
            const flow = appRoot.push(Qt.resolvedUrl("onboarding/OnboardingFlow.qml"));
            flow.completed.connect(function () {
                appRoot.pop();
                App.markOnboardingComplete();
            });
        }
    }
}
