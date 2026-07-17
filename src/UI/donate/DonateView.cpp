// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/donate/DonateView.h"

#include "UI/Theme.h"
#include "UI/common/ExternalLink.h"

#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>

namespace dish::ui {

namespace {

// A faint fill derived from a token at the given alpha, as a QSS `rgba(...)`
// fragment. Theme.cpp keeps an identical private helper for its chip fills, but
// it isn't exported and this view must not edit Theme; we recompute it locally
// from the active token so a light re-theme retints (the alpha tint follows
// `Theme::primary`, never a hardcoded literal).
QString tokenFill(QRgb c, double alpha) {
    const QColor q(c);
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(q.red())
        .arg(q.green())
        .arg(q.blue())
        .arg(alpha, 0, 'f', 2);
}

// The "Open X →" visit link: borderless, accent-coloured text (android renders
// donateRailVisit as colorPulse body text, no button chrome). Distinct from the
// outlined CTA button so it reads as an inline link.
QString linkButtonQss() {
    return QStringLiteral("QPushButton { background: transparent; border: none; color: %1; "
                          "font-size: 12px; font-weight: 600; padding: 2px 0; text-align: left; }")
        .arg(hex(Theme::primary));
}

// Make a whole card behave like android's `android:clickable` MaterialCardView:
// a click anywhere on the card (that isn't an inner button) runs `onClick`. An
// event filter parented to the card keeps lifetime parity. The visible "Open X
// →" link inside stays a real button for keyboard / screen-reader access.
class CardClickFilter : public QObject {
  public:
    CardClickFilter(QWidget* card, std::function<void()> onClick)
        : QObject(card), onClick_(std::move(onClick)) {
        card->setCursor(Qt::PointingHandCursor);
        card->installEventFilter(this);
    }
    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton) {
                onClick_();
                return true;
            }
        }
        return QObject::eventFilter(watched, event);
    }

  private:
    std::function<void()> onClick_;
};

// One meta column: an uppercase label (android `colorTertiary` -> the amber
// `warning` token) above its value (onSurface). Mirrors the two-column
// Cadence | Pays-with grid in donate_rail_card.xml.
QWidget* makeMetaColumn(QWidget* parent, const QString& label, const QString& value) {
    auto* col = new QWidget(parent);
    auto* box = new QVBoxLayout(col);
    box->setContentsMargins(0, 0, 0, 0);
    box->setSpacing(2);
    auto* labelLabel = new QLabel(label, col);
    labelLabel->setStyleSheet(QStringLiteral("color: %1; font-family: monospace; font-size: 10px; "
                                             "letter-spacing: 1px; font-weight: 600;")
                                  .arg(hex(Theme::warning)));
    auto* valueLabel = new QLabel(value, col);
    valueLabel->setWordWrap(true);
    valueLabel->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::onSurface)));
    box->addWidget(labelLabel);
    box->addWidget(valueLabel);
    return col;
}

// One "why we ask" row: a primary-tinted dot (android's bg_pulse_dot -> the
// accent token) and a body line. Mirrors donate_why_row.xml.
QWidget* makeWhyRow(QWidget* parent, const QString& text) {
    auto* row = new QWidget(parent);
    auto* line = new QHBoxLayout(row);
    line->setContentsMargins(0, 0, 0, 0);
    line->setSpacing(10);
    auto* dot = new QLabel(row);
    dot->setFixedSize(8, 8);
    dot->setStyleSheet(dotQss(Theme::primary));
    auto* body = new QLabel(text, row);
    body->setWordWrap(true);
    body->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::onSurface)));
    line->addWidget(dot, 0, Qt::AlignTop);
    line->addWidget(body, 1);
    return row;
}

} // namespace

DonateView::DonateView(NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent), notifications_(notifications) {
    setWindowTitle(QCoreApplication::translate("dish::ui::DonateView", "Support Dish"));
    setMinimumSize(440, 600);

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
    layout->setContentsMargins(24, 12, 24, 24);
    layout->setSpacing(12);

    // ── Hero (centered) ──────────────────────────────────────────────────
    // android: bg_pulse_badge glow + ic_heart, eyebrow, H1, lead, CTA — all
    // centered. The pulse-pink accent has no portable Theme token on Windows,
    // so the heart / eyebrow / CTA use the cyan `primary` accent (the same
    // substitution the rest of dish-windows makes) — theme-correct under dark
    // AND light because it reads the active palette.
    auto* hero = new QWidget(body);
    auto* heroCol = new QVBoxLayout(hero);
    heroCol->setContentsMargins(0, 12, 0, 12);
    heroCol->setSpacing(10);

    auto* heart = new QLabel(QStringLiteral("♥"), hero); // ♥ — brand glyph, not localized
    heart->setAlignment(Qt::AlignCenter);
    heart->setFixedSize(64, 64);
    heart->setStyleSheet(QStringLiteral("background-color: %1; color: %2; border-radius: 32px; "
                                        "font-size: 30px;")
                             .arg(tokenFill(Theme::primary, 0.14), hex(Theme::primary)));
    heroCol->addWidget(heart, 0, Qt::AlignHCenter);

    auto* eyebrow = new QLabel(tr("SUPPORT THE PROJECT"), hero);
    eyebrow->setAlignment(Qt::AlignCenter);
    eyebrow->setStyleSheet(sectionHeaderQss());
    heroCol->addWidget(eyebrow);

    auto* h1 = new QLabel(tr("Dish runs on coffee and goodwill."), hero);
    h1->setWordWrap(true);
    h1->setAlignment(Qt::AlignCenter);
    h1->setStyleSheet(
        QStringLiteral("font-size: 18px; font-weight: 700; color: %1;").arg(hex(Theme::onSurface)));
    heroCol->addWidget(h1);

    auto* lead =
        new QLabel(tr("Dish, Satellite, and every Dish client are free, open source, ad-free, "
                      "and analytics-free. No paywalled features, no upsells. Donations are what "
                      "keep them that way."),
                   hero);
    lead->setWordWrap(true);
    lead->setAlignment(Qt::AlignCenter);
    lead->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    heroCol->addWidget(lead);

    // Recommended CTA — opens GitHub Sponsors. The ♥ glyph mirrors android's
    // app:icon="@drawable/ic_heart" on btnDonateCta; it's prepended outside tr()
    // so it's not part of the (already-translated) label string.
    auto* cta = new QPushButton(
        QStringLiteral("♥  ") + tr("Sponsor on GitHub Sponsors (recommended)"), hero);
    cta->setObjectName(QStringLiteral("primary"));
    cta->setCursor(Qt::PointingHandCursor);
    QObject::connect(cta, &QPushButton::clicked, this,
                     [this, urlSponsors] { openExternalUrl(urlSponsors, notifications_); });
    heroCol->addWidget(cta, 0, Qt::AlignHCenter);

    layout->addWidget(hero);

    // ── Rails (display cards) ────────────────────────────────────────────
    // Each card is fully clickable (android donate_rail_card is clickable) and
    // still carries an explicit, accessible "Open X →" link. Names are brand
    // strings (not localized).
    const auto addRail = [&](const QString& name, const QString& blurb, const QString& cadence,
                             const QString& currencies, const QString& openLabel,
                             const QString& url, bool recommended) {
        auto* card = new QFrame(body);
        card->setObjectName(QStringLiteral("card"));
        auto* col = new QVBoxLayout(card);
        col->setContentsMargins(14, 12, 14, 12);
        col->setSpacing(8);

        // Title row: name (accent) + optional Recommended badge.
        auto* titleRow = new QHBoxLayout;
        auto* title = new QLabel(name, card);
        title->setStyleSheet(QStringLiteral("font-weight: 600; font-size: 15px; color: %1;")
                                 .arg(hex(Theme::primary)));
        titleRow->addWidget(title, 0, Qt::AlignVCenter);
        if (recommended) {
            auto* badge = new QLabel(
                QCoreApplication::translate("dish::ui::DonateView", "Recommended"), card);
            badge->setStyleSheet(capabilityChipQss(true));
            titleRow->addWidget(badge, 0, Qt::AlignVCenter);
        }
        titleRow->addStretch(1);
        col->addLayout(titleRow);

        auto* blurbLabel = new QLabel(blurb, card);
        blurbLabel->setWordWrap(true);
        blurbLabel->setStyleSheet(
            QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
        col->addWidget(blurbLabel);

        // Two-column Cadence | Pays-with meta grid.
        auto* metaRow = new QHBoxLayout;
        metaRow->setSpacing(12);
        metaRow->addWidget(makeMetaColumn(card, tr("CADENCE"), cadence), 1);
        metaRow->addWidget(makeMetaColumn(card, tr("PAYS WITH"), currencies), 1);
        col->addLayout(metaRow);

        // Visible visit link (accent), keyboard/screen-reader accessible.
        auto* open = new QPushButton(openLabel, card);
        open->setStyleSheet(linkButtonQss());
        open->setCursor(Qt::PointingHandCursor);
        QObject::connect(open, &QPushButton::clicked, this,
                         [this, url] { openExternalUrl(url, notifications_); });
        col->addWidget(open, 0, Qt::AlignLeft);

        // Whole-card click opens the same URL (android parity).
        new CardClickFilter(card, [this, url] { openExternalUrl(url, notifications_); });
        layout->addWidget(card);
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

    // ── "What your donation pays for" card ───────────────────────────────
    // android wraps the why-rows in a single Widget.Dish.Card with a title.
    auto* whyCard = new QFrame(body);
    whyCard->setObjectName(QStringLiteral("card"));
    auto* whyCol = new QVBoxLayout(whyCard);
    whyCol->setContentsMargins(14, 12, 14, 12);
    whyCol->setSpacing(10);
    auto* whyTitle = new QLabel(tr("What your donation pays for"), whyCard);
    whyTitle->setWordWrap(true);
    whyTitle->setStyleSheet(
        QStringLiteral("font-weight: 600; font-size: 15px; color: %1;").arg(hex(Theme::onSurface)));
    whyCol->addWidget(whyTitle);
    whyCol->addWidget(makeWhyRow(
        whyCard, tr("Hosting. dish.tinkernorth.com, tinkernorth.com, and every signed-installer "
                    "mirror. AWS isn't free, even at our scale.")));
    whyCol->addWidget(makeWhyRow(
        whyCard, tr("Code-signing certificates. Windows SmartScreen only plays nice because we pay "
                    "for an EV certificate every year.")));
    whyCol->addWidget(makeWhyRow(
        whyCard, tr("Store developer fees. The platform accounts and the time to keep each listing "
                    "compliant with every new policy round.")));
    whyCol->addWidget(makeWhyRow(
        whyCard, tr("Time. Honest answer: most of it. Dish is a nights-and-weekends project. "
                    "Donations let us say yes to working on it.")));
    layout->addWidget(whyCard);

    // ── Thanks (centered) ────────────────────────────────────────────────
    // android's donate_thanks closing line, verbatim shared brand copy.
    auto* thanks = new QLabel(tr("Thank you. Every dollar, every star, every shared link. They all "
                                 "add up. Emir"),
                              body);
    thanks->setWordWrap(true);
    thanks->setAlignment(Qt::AlignCenter);
    thanks->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; padding-top: 4px;").arg(hex(Theme::muted)));
    layout->addWidget(thanks);

    layout->addStretch(1);
    scroll->setWidget(body);
    outer->addWidget(scroll);

    auto* footer = new QHBoxLayout;
    footer->setContentsMargins(24, 0, 24, 16);
    footer->addStretch(1);
    auto* close = new QPushButton(tr("Close"), this);
    QObject::connect(close, &QPushButton::clicked, this, &QDialog::accept);
    footer->addWidget(close);
    outer->addLayout(footer);
}

} // namespace dish::ui
