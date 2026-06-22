// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// WelcomeDialog — the first-run welcome pager (Workstream 3a, D3 full
// onboarding). A QStackedWidget-backed 4-page pager with step-indicator dots and
// Back / Next / Skip navigation, mirroring dish-android ui/onboarding/
// WelcomeActivity.kt + WelcomePagerAdapter.kt (the WelcomePage(hero, eyebrow,
// title, body, extraCta) shape).
//
// The 4 pages (adapted for Windows physical-controller use): (1) "what Dish is",
// (2) the controller<->PC link concept (android's phone<->PC), (3) "install the
// satellite" + an external Open-download CTA, (4) "you're set" + a launch CTA
// that flows into the adapted setup wizard. Skip / final-Next mark the welcome
// complete and route to the dashboard; the launch CTA opens the wizard first.
//
// SoC: binds to OnboardingPreferenceStore (markWelcomeCompleted) and routes
// external links through the notification host on failure; never reads QSettings
// directly.

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

class NotificationQueue;

class WelcomeDialog : public QDialog {
    Q_OBJECT
  public:
    // `onboarding` (borrowed) is marked complete on Skip / Finish / launch.
    // `notifications` (borrowed, may be null) receives external-url-failed.
    WelcomeDialog(source::OnboardingPreferenceStore* onboarding, NotificationQueue* notifications,
                  QWidget* parent = nullptr);

  signals:
    // The final-page launch CTA was tapped: the host opens the setup wizard
    // after the dialog closes. (Skip / final-Next close without this signal.)
    void launchWizardRequested();

  private:
    void goToPage(int index);
    void updateNav();
    void completeAndClose(bool launchWizard);

    source::OnboardingPreferenceStore* onboarding_;
    NotificationQueue* notifications_;

    QStackedWidget* pager_;
    QPushButton* backButton_;
    QPushButton* nextButton_;
    QPushButton* skipButton_;
    std::vector<QLabel*> dots_;
    int pageCount_ = 0;
};

} // namespace dish::ui
