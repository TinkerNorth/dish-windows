// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The first-run onboarding flow, shown FULL-SCREEN OVER the nav shell (pushed
// onto Main.qml's appRoot StackView). It is JUST the 4-page welcome pager —
// setup itself is the pushed SetupWizardPage and Help & FAQ hangs off Settings
// — so the flow's only job is to host the pager and report how the user left
// it. It does NOT decide whether onboarding is needed and does NOT persist the
// "welcome completed" flag; Main.qml owns popping + persistence + the hand-off.
//
// Entry API for Main.qml:
//   var flow = appRoot.push("onboarding/OnboardingFlow.qml")
//   flow.completed.connect(function (runSetup) { appRoot.pop(); ... })
// `completed(runSetup)` fires exactly once: runSetup=false for Skip (still a
// completion — the flag is written either way), true when the final page's
// Finish asked to go straight into the setup wizard.

import QtQuick

Item {
    id: flow

    signal completed(bool runSetup)

    // Transparent so the window surface (Mica or themed solid) shows through;
    // the pager paints no opaque full-area panel.

    WelcomeScreen {
        anchors.fill: parent
        onFinished: runSetup => flow.completed(runSetup)
    }
}
