// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ui/onboarding/SetupWizardView.h"

#include "UI/Theme.h"
#include "source/store/OnboardingPreferenceStore.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <vector>

namespace dish::ui {

namespace {

// A wizard option card: title + body, with a primary-tinted selection stroke
// (the highlighted walkthrough item). Ports android's WizardOptionCard look
// (the strokeColor swap to colorPrimary on the picked card) — repurposed here as
// the "primary action for this step" card rather than a choice.
QFrame* makeOptionCard(QWidget* parent, const QString& title, const QString& body, bool stroked) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    const QString stroke = stroked ? hex(Theme::primary) : hex(Theme::outline);
    card->setStyleSheet(QStringLiteral("QFrame#card { background-color: %1; border: %2px solid %3; "
                                       "border-radius: 8px; }")
                            .arg(hex(Theme::surface))
                            .arg(stroked ? 2 : 1)
                            .arg(stroke));
    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(14, 12, 14, 12);
    col->setSpacing(4);
    auto* titleLabel = new QLabel(title, card);
    titleLabel->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* bodyLabel = new QLabel(body, card);
    bodyLabel->setWordWrap(true);
    bodyLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    col->addWidget(titleLabel);
    col->addWidget(bodyLabel);
    return card;
}

// A step page: eyebrow + title + body + the option cards.
QWidget* makeStep(QWidget* parent, const QString& eyebrow, const QString& title,
                  const QString& body, const std::vector<QFrame*>& cards) {
    auto* page = new QWidget(parent);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(10);
    auto* eyebrowLabel = new QLabel(eyebrow, page);
    eyebrowLabel->setStyleSheet(sectionHeaderQss());
    col->addWidget(eyebrowLabel);
    auto* titleLabel = new QLabel(title, page);
    titleLabel->setWordWrap(true);
    titleLabel->setStyleSheet(
        QStringLiteral("font-size: 17px; font-weight: 700; color: %1;").arg(hex(Theme::onSurface)));
    col->addWidget(titleLabel);
    if (!body.isEmpty()) {
        auto* bodyLabel = new QLabel(body, page);
        bodyLabel->setWordWrap(true);
        bodyLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
        col->addWidget(bodyLabel);
    }
    for (QFrame* card : cards) {
        card->setParent(page);
        col->addWidget(card);
    }
    col->addStretch(1);
    return page;
}

} // namespace

SetupWizardView::SetupWizardView(source::OnboardingPreferenceStore* onboarding, QWidget* parent)
    : QDialog(parent), onboarding_(onboarding) {
    setWindowTitle(tr("Setup guide"));
    setMinimumSize(440, 480);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(14);

    progress_ = new QLabel(this);
    progress_->setStyleSheet(sectionHeaderQss());
    root->addWidget(progress_);

    steps_ = new QStackedWidget(this);

    // Step 1 — satellite discovery / pairing walkthrough (repurposed from
    // android's LAN-vs-BT connection picker).
    steps_->addWidget(makeStep(
        steps_, tr("STEP 1 · CONNECT"), tr("Find and pair your Satellite"),
        tr("Dish reaches a host PC over your local network. Both machines must be on the same "
           "Wi-Fi or LAN."),
        {makeOptionCard(steps_, tr("Find your PC on the network"),
                        tr("Open Connections and tap Scan. Satellites running on your LAN appear "
                           "automatically — no IP to type in."),
                        true),
         makeOptionCard(steps_, tr("Enter the operator PIN"),
                        tr("Pick your Satellite and enter the 6-digit PIN it shows on the host "
                           "screen. Once accepted, the pairing is remembered."),
                        false)}));

    // Step 2 — first-controller / discovery walkthrough (repurposed from
    // android's controller-source picker, meaningless on Windows: SDL surfaces
    // physical pads transparently).
    steps_->addWidget(makeStep(
        steps_, tr("STEP 2 · CONTROLLER"), tr("Plug in or pair a controller"),
        tr("There is no on-screen pad to pick on Windows — Dish forwards any real controller this "
           "PC sees."),
        {makeOptionCard(steps_, tr("It appears automatically"),
                        tr("Connect an Xbox, PlayStation, or generic gamepad over USB or "
                           "Bluetooth. Windows detects it and Dish lists it as a new slot."),
                        true),
         makeOptionCard(
             steps_, tr("Confirm it's detected"),
             tr("The controller shows up on the dashboard with its capabilities (motion, "
                "lightbar, battery). Bind it to a paired Satellite to start streaming."),
             false)}));

    // Step 3 — summary + a single Windows-relevant "what's next" line (the
    // android cross-product matrix is dropped).
    steps_->addWidget(makeStep(
        steps_, tr("STEP 3 · LET'S GO"), tr("Here's what we'll do"), QString(),
        {makeOptionCard(steps_, tr("Summary"),
                        tr("Connection: Wi-Fi / LAN to Satellite.\nController: any pad this PC "
                           "detects."),
                        false),
         makeOptionCard(steps_, tr("What's next"),
                        tr("Tap Finish to open Connections. Scan for your Satellite, pair it with "
                           "the PIN, then bind a connected controller to start playing."),
                        true)}));

    stepCount_ = steps_->count();
    root->addWidget(steps_, 1);

    auto* navRow = new QHBoxLayout;
    backButton_ = new QPushButton(tr("Back"), this);
    QObject::connect(backButton_, &QPushButton::clicked, this, [this] {
        if (step_ > 0) { applyStep(step_ - 1); }
    });
    nextButton_ = new QPushButton(this);
    nextButton_->setObjectName(QStringLiteral("primary"));
    QObject::connect(nextButton_, &QPushButton::clicked, this, &SetupWizardView::onPrimaryAction);
    navRow->addWidget(backButton_, 0, Qt::AlignLeft);
    navRow->addStretch(1);
    navRow->addWidget(nextButton_, 0, Qt::AlignRight);
    root->addLayout(navRow);

    applyStep(0);
}

void SetupWizardView::applyStep(int target) {
    step_ = qBound(0, target, stepCount_ - 1);
    steps_->setCurrentIndex(step_);
    progress_->setText(tr("Step %1 of %2").arg(step_ + 1).arg(stepCount_));
    refreshPrimaryAction();
}

void SetupWizardView::refreshPrimaryAction() {
    const bool isFinal = step_ == stepCount_ - 1;
    backButton_->setVisible(step_ > 0);
    nextButton_->setText(isFinal ? tr("Finish") : tr("Next"));
}

void SetupWizardView::onPrimaryAction() {
    if (step_ < stepCount_ - 1) {
        applyStep(step_ + 1);
        return;
    }
    // Finish: mark complete (idempotent if the pager already did) + route on.
    if (onboarding_ != nullptr) { onboarding_->markWelcomeCompleted(); }
    emit finished();
    accept();
}

} // namespace dish::ui
