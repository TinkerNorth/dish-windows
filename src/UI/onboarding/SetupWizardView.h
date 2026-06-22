// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// SetupWizardView — the adapted setup wizard (Workstream 3a, D3 full onboarding,
// adapted). Ports dish-android ui/onboarding/SetupWizardActivity.kt's step
// framework (applyStep / refreshPrimaryAction / renderSummary, the "step N of M"
// progress indicator, option cards with a selection stroke, Back/Next/Finish
// nav, and a final summary + next-steps step) — but REPURPOSES android's two
// picker steps, which are meaningless on Windows:
//   * android step 1 (LAN vs BT connection) -> a satellite discovery / pairing
//     WALKTHROUGH (find your PC on the LAN -> enter the operator PIN -> paired).
//   * android step 2 (controller-source picker VIRTUAL/USB/PHYSICAL_BT) -> a
//     first-controller / discovery WALKTHROUGH (plug in or pair a controller; it
//     appears automatically; how to confirm it is detected).
// The android isCombinationViable() LAN/BT cross-product + the per-combination
// nextStepsCopy() matrix are dropped (they encode the dropped BT/source choice);
// a single Windows-relevant summary + "what's next" line is kept.
//
// On Finish: markWelcomeCompleted() (idempotent if the pager already set it) and
// emit finished() so the host routes to the dashboard / connections surface.

#pragma once

#include <QDialog>

#include <vector>

class QLabel;
class QPushButton;
class QStackedWidget;

namespace dish::source {
class OnboardingPreferenceStore;
}

namespace dish::ui {

class SetupWizardView : public QDialog {
    Q_OBJECT
  public:
    // `onboarding` (borrowed, may be null in a standalone re-run) is marked
    // complete on Finish.
    explicit SetupWizardView(source::OnboardingPreferenceStore* onboarding,
                             QWidget* parent = nullptr);

  signals:
    // Emitted on Finish (after markWelcomeCompleted): the host routes onward.
    void finished();

  private:
    void applyStep(int target);
    void refreshPrimaryAction();
    void onPrimaryAction();

    source::OnboardingPreferenceStore* onboarding_;

    QStackedWidget* steps_;
    QLabel* progress_;
    QPushButton* backButton_;
    QPushButton* nextButton_;
    int step_ = 0; // 0-based current step index
    int stepCount_ = 0;
};

} // namespace dish::ui
