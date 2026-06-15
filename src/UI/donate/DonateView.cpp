// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ui/donate/DonateView.h"

#include "UI/Theme.h"
#include "ui/common/ExternalLink.h"

#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// A "rail" display card: name + optional recommended badge, blurb, and a
// cadence + currencies meta line. The action (open the URL) is a labelled button
// added by the caller below — keeping the card itself a non-interactive renderer.
QFrame* makeRail(QWidget* parent, const QString& name, const QString& blurb, const QString& cadence,
                 const QString& currencies, bool recommended) {
    auto* card = new QFrame(parent);
    card->setObjectName(QStringLiteral("card"));
    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(14, 12, 14, 12);
    col->setSpacing(6);

    auto* titleRow = new QHBoxLayout;
    auto* title = new QLabel(name, card);
    title->setStyleSheet(
        QStringLiteral("font-weight: 600; font-size: 15px; color: %1;").arg(hex(Theme::onSurface)));
    titleRow->addWidget(title, 0, Qt::AlignVCenter);
    if (recommended) {
        auto* badge =
            new QLabel(QCoreApplication::translate("dish::ui::DonateView", "Recommended"), card);
        badge->setStyleSheet(capabilityChipQss(true));
        titleRow->addWidget(badge, 0, Qt::AlignVCenter);
    }
    titleRow->addStretch(1);
    col->addLayout(titleRow);

    auto* blurbLabel = new QLabel(blurb, card);
    blurbLabel->setWordWrap(true);
    blurbLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    col->addWidget(blurbLabel);

    auto* meta = new QLabel(
        QCoreApplication::translate("dish::ui::DonateView", "Cadence: %1   ·   Pays with: %2")
            .arg(cadence, currencies),
        card);
    meta->setWordWrap(true);
    meta->setStyleSheet(QStringLiteral("color: %1; font-family: monospace; font-size: 11px;")
                            .arg(hex(Theme::muted)));
    col->addWidget(meta);
    return card;
}

QLabel* makeWhy(QWidget* parent, const QString& text) {
    auto* label = new QLabel(QStringLiteral("•  %1").arg(text), parent);
    label->setWordWrap(true);
    label->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    return label;
}

} // namespace

DonateView::DonateView(NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent), notifications_(notifications) {
    setWindowTitle(QCoreApplication::translate("dish::ui::DonateView", "Support Dish"));
    setMinimumSize(440, 560);

    // Localizable URLs — default to the android values; a locale .ts may override.
    const QString urlSponsors =
        tr("https://github.com/sponsors/TinkerNorth", "url_sponsors (localizable override)");
    const QString urlKofi = tr("https://ko-fi.com/tinkernorth", "url_kofi (localizable override)");
    const QString urlBmac =
        tr("https://buymeacoffee.com/tinkernorth", "url_bmac (localizable override)");

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* eyebrow = new QLabel(tr("SUPPORT THE PROJECT"), body);
    eyebrow->setStyleSheet(sectionHeaderQss());
    layout->addWidget(eyebrow);

    auto* h1 = new QLabel(tr("Dish runs on coffee and goodwill."), body);
    h1->setWordWrap(true);
    h1->setStyleSheet(
        QStringLiteral("font-size: 18px; font-weight: 700; color: %1;").arg(hex(Theme::onSurface)));
    layout->addWidget(h1);

    auto* lead =
        new QLabel(tr("Dish, Satellite, and every Dish client are free, open source, ad-free, "
                      "and analytics-free. No paywalled features, no upsells. Donations are what "
                      "keep them that way."),
                   body);
    lead->setWordWrap(true);
    lead->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    layout->addWidget(lead);

    // Recommended CTA — opens GitHub Sponsors.
    auto* cta = new QPushButton(tr("Sponsor on GitHub Sponsors (recommended)"), body);
    cta->setObjectName(QStringLiteral("primary"));
    QObject::connect(cta, &QPushButton::clicked, this,
                     [this, urlSponsors] { openExternalUrl(urlSponsors, notifications_); });
    layout->addWidget(cta);

    // Three rails (display cards) each followed by an explicit, accessible "open"
    // button bound to its URL. Names are brand strings (not localized).
    const auto addRail = [&](const QString& name, const QString& blurb, const QString& cadence,
                             const QString& currencies, const QString& openLabel,
                             const QString& url, bool recommended) {
        layout->addWidget(makeRail(body, name, blurb, cadence, currencies, recommended));
        auto* open = new QPushButton(openLabel, body);
        open->setStyleSheet(outlinedButtonQss());
        QObject::connect(open, &QPushButton::clicked, this,
                         [this, url] { openExternalUrl(url, notifications_); });
        layout->addWidget(open, 0, Qt::AlignLeft);
    };

    addRail(QStringLiteral("GitHub Sponsors"),
            tr("Monthly sponsorship, processed by GitHub. Lowest fees, no platform cut — the most "
               "stable way to support Dish, and the one GitHub matches dollar-for-dollar where "
               "eligible."),
            tr("Recurring"), tr("Card, PayPal, GitHub credit"), tr("Open GitHub Sponsors →"),
            urlSponsors, true);
    addRail(QStringLiteral("Ko-fi"),
            tr("Tip jar. Buy us a coffee, no account required. Ko-fi takes no cut on one-time "
               "tips, so every dollar reaches the project."),
            tr("One-time"), tr("Card, Apple Pay, Google Pay, PayPal"), tr("Open Ko-fi →"), urlKofi,
            false);
    addRail(QStringLiteral("Buy Me a Coffee"),
            tr("Quick one-time gift or a monthly membership. Card, Apple Pay, or Google Pay. "
               "Great if you don't have a GitHub account."),
            tr("Either"), tr("Card, Apple Pay, Google Pay"), tr("Open Buy Me a Coffee →"), urlBmac,
            false);

    // "Why we ask" cards.
    auto* whyHeader = new QLabel(tr("WHAT YOUR DONATION PAYS FOR"), body);
    whyHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(whyHeader);
    layout->addWidget(makeWhy(
        body, tr("Hosting. dish.tinkernorth.com, tinkernorth.com, and every signed-installer "
                 "mirror. AWS isn't free, even at our scale.")));
    layout->addWidget(makeWhy(
        body, tr("Code-signing certificates. Windows SmartScreen only plays nice because we pay "
                 "for an EV certificate every year.")));
    layout->addWidget(makeWhy(
        body, tr("Store developer fees. The platform accounts and the time to keep each listing "
                 "compliant with every new policy round.")));
    layout->addWidget(
        makeWhy(body, tr("Time. Honest answer: most of it. Dish is a nights-and-weekends project. "
                         "Donations let us say yes to working on it.")));

    layout->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(20, 0, 20, 16);
    footer->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    QObject::connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    outer->addLayout(footer);
}

} // namespace dish::ui
