// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ui/onboarding/WelcomeDialog.h"

#include "UI/Theme.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "ui/common/ExternalLink.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <cstddef>
#include <functional>
#include <utility>

namespace dish::ui {

namespace {

// One welcome page's content. Mirrors android's WelcomePage(hero, eyebrow,
// title, body, extraCta) — hero art is omitted (no bundled onboarding glyphs;
// flagged in the report), the rest ports.
struct PageSpec {
    QString eyebrow;
    QString title;
    QString body;
    QString extraCtaLabel; // empty = no extra CTA
    enum class Cta { None, OpenSatellite, LaunchWizard } cta = Cta::None;
};

QWidget* buildPage(const PageSpec& spec, QWidget* parent,
                   std::function<void(PageSpec::Cta)> onCta) {
    auto* page = new QWidget(parent);
    auto* col = new QVBoxLayout(page);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(10);
    col->addStretch(1);

    auto* eyebrow = new QLabel(spec.eyebrow, page);
    eyebrow->setStyleSheet(sectionHeaderQss());
    eyebrow->setAlignment(Qt::AlignHCenter);
    col->addWidget(eyebrow);

    auto* title = new QLabel(spec.title, page);
    title->setWordWrap(true);
    title->setAlignment(Qt::AlignHCenter);
    title->setStyleSheet(
        QStringLiteral("font-size: 20px; font-weight: 700; color: %1;").arg(hex(Theme::onSurface)));
    col->addWidget(title);

    auto* body = new QLabel(spec.body, page);
    body->setWordWrap(true);
    body->setAlignment(Qt::AlignHCenter);
    body->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(hex(Theme::muted)));
    col->addWidget(body);

    if (!spec.extraCtaLabel.isEmpty()) {
        auto* cta = new QPushButton(spec.extraCtaLabel, page);
        const PageSpec::Cta kind = spec.cta;
        QObject::connect(cta, &QPushButton::clicked, page, [onCta, kind] { onCta(kind); });
        col->addWidget(cta, 0, Qt::AlignHCenter);
    }

    col->addStretch(2);
    return page;
}

} // namespace

WelcomeDialog::WelcomeDialog(source::OnboardingPreferenceStore* onboarding,
                             NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent), onboarding_(onboarding), notifications_(notifications) {
    setWindowTitle(QStringLiteral("Dish")); // brand name — not localized
    setMinimumSize(420, 460);

    // Page 2's "phone<->PC link" concept becomes "controller<->PC link"; page 4's
    // launch CTA flows into the adapted setup wizard. Pages 1 & 3 port in spirit.
    std::vector<PageSpec> specs = {
        {tr("WELCOME"),
         tr("Your controller, on your PC"),
         tr("Dish forwards a real game controller plugged into this PC to another PC running "
            "Satellite, over your local network. Plug in a pad and it shows up ready to play."),
         {},
         PageSpec::Cta::None},
        {tr("HOW IT WORKS"),
         tr("A short hop over Wi-Fi"),
         tr("Your controller's button presses, sticks, and motion travel to a small free helper "
            "called Satellite running on the host PC. Satellite shows up to its games as a "
            "regular gamepad. No extra setup per game."),
         {},
         PageSpec::Cta::None},
        {tr("ONE MORE THING"), tr("Install Satellite on the host PC"),
         tr("Satellite is free and open source. Grab it from tinkernorth.com/satellite, run the "
            "installer, and Dish will find it automatically. You can finish this intro now and "
            "install Satellite later."),
         tr("Open download page"), PageSpec::Cta::OpenSatellite},
        {tr("YOU'RE SET"), tr("Ready when you are"),
         tr("Open the setup guide for a walkthrough, or jump straight in and pair with a "
            "Satellite on your network."),
         tr("Open setup guide"), PageSpec::Cta::LaunchWizard},
    };
    pageCount_ = static_cast<int>(specs.size());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 20);
    root->setSpacing(16);

    pager_ = new QStackedWidget(this);
    const QString urlSatellite =
        tr("https://tinkernorth.com/satellite", "url_satellite (localizable override)");
    auto onCta = [this, urlSatellite](PageSpec::Cta kind) {
        if (kind == PageSpec::Cta::OpenSatellite) {
            openExternalUrl(urlSatellite, notifications_);
        } else if (kind == PageSpec::Cta::LaunchWizard) {
            completeAndClose(/*launchWizard=*/true);
        }
    };
    for (const PageSpec& spec : specs) { pager_->addWidget(buildPage(spec, pager_, onCta)); }
    root->addWidget(pager_, 1);

    // Step-indicator dots.
    auto* dotsRow = new QHBoxLayout;
    dotsRow->addStretch(1);
    for (int i = 0; i < pageCount_; ++i) {
        auto* dot = new QLabel(this);
        dot->setFixedSize(8, 8);
        dots_.push_back(dot);
        dotsRow->addWidget(dot, 0, Qt::AlignVCenter);
    }
    dotsRow->addStretch(1);
    root->addLayout(dotsRow);

    // Nav: Back / Skip / Next.
    auto* navRow = new QHBoxLayout;
    backButton_ = new QPushButton(tr("Back"), this);
    QObject::connect(backButton_, &QPushButton::clicked, this,
                     [this] { goToPage(pager_->currentIndex() - 1); });
    skipButton_ = new QPushButton(tr("Skip"), this);
    QObject::connect(skipButton_, &QPushButton::clicked, this,
                     [this] { completeAndClose(/*launchWizard=*/false); });
    nextButton_ = new QPushButton(tr("Next"), this);
    nextButton_->setObjectName(QStringLiteral("primary"));
    QObject::connect(nextButton_, &QPushButton::clicked, this, [this] {
        const int current = pager_->currentIndex();
        if (current < pageCount_ - 1) {
            goToPage(current + 1);
        } else {
            completeAndClose(/*launchWizard=*/false);
        }
    });
    navRow->addWidget(backButton_, 0, Qt::AlignLeft);
    navRow->addStretch(1);
    navRow->addWidget(skipButton_, 0, Qt::AlignVCenter);
    navRow->addWidget(nextButton_, 0, Qt::AlignRight);
    root->addLayout(navRow);

    goToPage(0);
}

void WelcomeDialog::goToPage(int index) {
    if (index < 0 || index >= pageCount_) { return; }
    pager_->setCurrentIndex(index);
    updateNav();
}

void WelcomeDialog::updateNav() {
    const int current = pager_->currentIndex();
    const bool isFinal = current == pageCount_ - 1;
    backButton_->setVisible(current > 0);
    skipButton_->setVisible(!isFinal);
    nextButton_->setText(isFinal ? tr("Get started") : tr("Next"));
    for (int i = 0; i < pageCount_; ++i) {
        dots_[static_cast<std::size_t>(i)]->setStyleSheet(
            dotQss(i == current ? Theme::primary : Theme::muted));
    }
}

void WelcomeDialog::completeAndClose(bool launchWizard) {
    if (onboarding_ != nullptr) { onboarding_->markWelcomeCompleted(); }
    if (launchWizard) { emit launchWizardRequested(); }
    accept();
}

} // namespace dish::ui
