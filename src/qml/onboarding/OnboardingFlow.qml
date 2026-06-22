// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The first-run onboarding flow, shown FULL-SCREEN OVER the nav shell (pushed
// onto Main.qml's appRoot StackView; see docs/QML_UI_KIT.md §5). It is a thin,
// purely-visual stepper — Welcome pager -> Setup guide -> Help/FAQ — with no
// business logic: it does NOT decide whether onboarding is needed (that is a
// C++/Main decision against the onboarding store) and it does NOT persist the
// "welcome completed" flag itself. It only walks the user through the screens
// and, when they finish or skip out, emits `completed` so Main.qml can pop it
// and (in the C++ wiring) mark onboarding done.
//
// Entry API for Main.qml:
//   var flow = appRoot.push("onboarding/OnboardingFlow.qml")
//   flow.completed.connect(function () { appRoot.pop() })
// The `completed` signal fires exactly once for both the Skip and the
// Finish/Done paths; Main owns popping + persistence.

// ComponentBehavior: Bound — the inline Components below reference the outer
// `steps`/`flow` ids; binding makes that access qualified (qmllint-clean).
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic

Item {
    id: flow

    // Emitted once when the user leaves onboarding (Skip on the pager, or Done
    // on Help). Main.qml connects this to pop the flow off appRoot and to mark
    // onboarding complete via the C++ surface (see report). The flow never
    // touches the store or pops itself — that keeps first-run policy in C++.
    signal completed()

    // Transparent so the OS Mica backdrop shows through; only the inner Kit.Card
    // surfaces paint. (Matches the page convention — never an opaque full area.)

    StackView {
        id: steps
        anchors.fill: parent
        background: null

        // The Welcome pager is the first screen. From it the user advances to
        // the Setup guide; from the guide to Help; Help's Done ends the flow.
        // Skip from any screen short-circuits straight to `completed`.
        initialItem: welcomeComponent
    }

    Component {
        id: welcomeComponent
        WelcomeScreen {
            onAdvance: steps.push(wizardComponent)
            onSkip: flow.completed()
        }
    }

    Component {
        id: wizardComponent
        SetupWizardScreen {
            onBack: steps.pop()
            onFinished: steps.push(helpComponent)
            onSkip: flow.completed()
        }
    }

    Component {
        id: helpComponent
        HelpScreen {
            onBack: steps.pop()
            onDone: flow.completed()
        }
    }
}
