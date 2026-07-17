// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/licenses/LicensesView.h"

#include "UI/Theme.h"
#include "UI/common/ExternalLink.h"
#include "UI/licenses/LicenseManifest.h"

#include <QCoreApplication>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

#include <functional>
#include <utility>

namespace dish::ui {

namespace {

// A card frame that emits clicked() on a left mouse release — used for the
// interactive (has-URL) license rows. Non-interactive rows use a plain QFrame.
class ClickableCard : public QFrame {
  public:
    explicit ClickableCard(QWidget* parent, std::function<void()> onClick)
        : QFrame(parent), onClick_(std::move(onClick)) {
        setObjectName(QStringLiteral("card"));
        setCursor(Qt::PointingHandCursor);
    }

  protected:
    void mouseReleaseEvent(QMouseEvent* event) override {
        if (event->button() == Qt::LeftButton && onClick_) { onClick_(); }
        QFrame::mouseReleaseEvent(event);
    }

  private:
    std::function<void()> onClick_;
};

QWidget* makeRow(QWidget* parent, const LicenseEntry& entry, NotificationQueue* notifications) {
    const QString name = licenseDisplayName(entry);
    const QString version = licenseVersionLabel(entry);
    const std::optional<QString> label = licenseLabel(entry);
    const std::optional<QString> url = licenseClickUrl(entry);

    QFrame* card = nullptr;
    if (url.has_value()) {
        const QString href = *url;
        card = new ClickableCard(parent,
                                 [notifications, href] { openExternalUrl(href, notifications); });
    } else {
        card = new QFrame(parent);
        card->setObjectName(QStringLiteral("card"));
    }

    auto* col = new QVBoxLayout(card);
    col->setContentsMargins(14, 10, 14, 10);
    col->setSpacing(2);

    auto* titleRow = new QHBoxLayout;
    auto* nameLabel = new QLabel(name, card);
    nameLabel->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    titleRow->addWidget(nameLabel, 1, Qt::AlignVCenter);
    if (!version.isEmpty()) {
        auto* versionLabel = new QLabel(version, card);
        versionLabel->setStyleSheet(
            QStringLiteral("color: %1; font-family: monospace; font-size: 11px;")
                .arg(hex(Theme::muted)));
        titleRow->addWidget(versionLabel, 0, Qt::AlignVCenter);
    }
    col->addLayout(titleRow);

    // License label hidden entirely when absent (don't render an empty line).
    if (label.has_value()) {
        auto* licenseLabelWidget = new QLabel(*label, card);
        licenseLabelWidget->setStyleSheet(
            QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
        col->addWidget(licenseLabelWidget);
    }
    return card;
}

} // namespace

LicensesView::LicensesView(NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent), notifications_(notifications) {
    setWindowTitle(QCoreApplication::translate("dish::ui::LicensesView", "Open source licenses"));
    setMinimumSize(440, 520);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    auto* intro = new QLabel(tr("Acknowledgements for the libraries Dish is built on. Tap a row "
                                "to open its license."),
                             body);
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    layout->addWidget(intro);

    const LicenseManifest manifest = loadBundledLicenseManifest();
    if (manifest.libraries.empty()) {
        auto* empty = new QLabel(tr("No license information bundled."), body);
        empty->setStyleSheet(QStringLiteral("color: %1;").arg(hex(Theme::muted)));
        layout->addWidget(empty);
    } else {
        for (const LicenseEntry& entry : manifest.libraries) {
            layout->addWidget(makeRow(body, entry, notifications_));
        }
    }

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
