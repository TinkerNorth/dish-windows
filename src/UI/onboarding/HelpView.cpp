// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "UI/onboarding/HelpView.h"

#include "UI/Theme.h"
#include "UI/common/ExternalLink.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// A collapsible FAQ row: a checkable header button that toggles a body label.
QWidget* makeFaq(QWidget* parent, const QString& question, const QString& answer) {
    auto* wrap = new QWidget(parent);
    auto* col = new QVBoxLayout(wrap);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(2);

    auto* header = new QToolButton(wrap);
    header->setText(question);
    header->setCheckable(true);
    header->setChecked(false);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setArrowType(Qt::RightArrow);
    header->setStyleSheet(QStringLiteral("QToolButton { color: %1; font-weight: 600; border: none; "
                                         "background: transparent; text-align: left; padding: 6px "
                                         "0; }")
                              .arg(hex(Theme::onSurface)));
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    auto* body = new QLabel(answer, wrap);
    body->setWordWrap(true);
    body->setVisible(false);
    body->setStyleSheet(
        QStringLiteral("color: %1; font-size: 12px; padding: 0 0 8px 4px;").arg(hex(Theme::muted)));

    QObject::connect(header, &QToolButton::toggled, wrap, [header, body](bool on) {
        body->setVisible(on);
        header->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });

    col->addWidget(header);
    col->addWidget(body);
    return wrap;
}

QLabel* makeSection(QWidget* parent, const QString& text) {
    auto* label = new QLabel(text, parent);
    label->setStyleSheet(sectionHeaderQss());
    return label;
}

} // namespace

HelpView::HelpView(NotificationQueue* notifications, QWidget* parent)
    : QDialog(parent), notifications_(notifications) {
    setWindowTitle(tr("Help & FAQ"));
    setMinimumSize(440, 540);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto* body = new QWidget(scroll);
    auto* layout = new QVBoxLayout(body);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(8);

    // "Run setup" card — relaunches the wizard.
    auto* runSetup = new QFrame(body);
    runSetup->setObjectName(QStringLiteral("card"));
    auto* runCol = new QVBoxLayout(runSetup);
    runCol->setContentsMargins(14, 12, 14, 12);
    runCol->setSpacing(4);
    auto* runTitle = new QLabel(tr("Walk me through setup"), runSetup);
    runTitle->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* runBody = new QLabel(
        tr("Open the setup guide. It points you at the right screen to connect and pair."),
        runSetup);
    runBody->setWordWrap(true);
    runBody->setStyleSheet(QStringLiteral("color: %1; font-size: 12px;").arg(hex(Theme::muted)));
    auto* runButton = new QPushButton(tr("Open setup guide"), runSetup);
    runButton->setStyleSheet(outlinedButtonQss());
    QObject::connect(runButton, &QPushButton::clicked, this, [this] {
        emit runSetupRequested();
        accept();
    });
    runCol->addWidget(runTitle);
    runCol->addWidget(runBody);
    runCol->addWidget(runButton, 0, Qt::AlignLeft);
    layout->addWidget(runSetup);

    // CONCEPTS (BT/Wi-Fi and on-screen/USB-claim rows dropped for Windows).
    layout->addWidget(makeSection(body, tr("CONCEPTS")));
    layout->addWidget(makeFaq(
        body, tr("What is Dish?"),
        tr("Dish turns a controller plugged into this PC into a wireless gamepad for another PC. "
           "It forwards input over your local network to the free Satellite helper.")));
    layout->addWidget(makeFaq(
        body, tr("What is Satellite?"),
        tr("Satellite is a small, free, open-source program that runs on the host PC. It receives "
           "input from Dish over Wi-Fi and presents itself to that PC's games as a regular "
           "controller. Without Satellite, games can't see your controller. Install it from "
           "tinkernorth.com/satellite.")));
    layout->addWidget(makeFaq(
        body, tr("What's motion and what's the touchpad?"),
        tr("Motion is a controller's gyroscope used as aim assist, forwarded as the gyro inside a "
           "PlayStation pad. The touchpad forwards a DualSense / DualShock 4 touchpad. Both need a "
           "PlayStation-style controller and a Satellite connection.")));

    // BEST PERFORMANCE.
    layout->addWidget(makeSection(body, tr("BEST PERFORMANCE")));
    layout->addWidget(makeFaq(
        body, tr("What's the best setup for low latency?"),
        tr("Both PCs on the same router, wired Ethernet where you can, 5 GHz Wi-Fi otherwise. Dish "
           "needs this PC and the host PC on the same local network — it sends input as small UDP "
           "packets, so a fast LAN keeps the delay tiny.")));
    layout->addWidget(
        makeFaq(body, tr("Is wired better than wireless?"),
                tr("For the controller-to-PC leg, a USB controller has no radio latency. For the "
                   "PC-to-PC leg, connect both machines to the router with Ethernet if you can.")));

    // TROUBLESHOOTING.
    layout->addWidget(makeSection(body, tr("TROUBLESHOOTING")));
    layout->addWidget(makeFaq(
        body, tr("Dish can't find any Satellites. What now?"),
        tr("Check that Satellite is running on the host PC (its tray icon should be visible). "
           "Confirm both PCs are on the same network. Guest networks and 2.4 GHz vs 5 GHz on the "
           "same router count as different. Tap Scan in Connections to retry.")));
    layout->addWidget(makeFaq(
        body, tr("My PIN keeps getting rejected."),
        tr("The PIN is shown by Satellite on the host PC and changes if you restart it. Check the "
           "digits right before you type, and make sure you're pairing with the right Satellite if "
           "more than one is listed.")));
    layout->addWidget(makeFaq(
        body, tr("My controller keeps disconnecting mid-game."),
        tr("Most disconnects come from Wi-Fi switching bands or the controller's own wireless "
           "dropping. Prefer wired Ethernet on both PCs and a wired controller. Dish keeps the "
           "session alive while the app is open.")));
    layout->addWidget(makeFaq(
        body, tr("Motion (gyro aim) isn't working."),
        tr("Motion needs three things: a Satellite connection, a controller with a gyroscope "
           "(DualSense, DualShock 4, Switch Pro), and a PlayStation-style controller type on the "
           "slot. Switch the emulated type to PlayStation to enable the motion channel.")));

    // ABOUT DISH.
    layout->addWidget(makeSection(body, tr("ABOUT DISH")));
    layout->addWidget(makeFaq(
        body, tr("Is Dish open source?"),
        tr("Yes. Dish (Windows, Android, Linux, Mac) and Satellite are all released under "
           "LGPL-3.0, source on GitHub at github.com/TinkerNorth. Pull requests welcome.")));
    layout->addWidget(makeFaq(
        body, tr("What does Dish collect?"),
        tr("Almost nothing. Dish doesn't send gameplay or controller input anywhere except the "
           "host PC on your local network. Anonymous crash reports are on by default and can be "
           "turned off (Settings → Diagnostics). See the privacy policy for the full list.")));

    // External-link cards.
    const QString urlPrivacy = tr("https://dish.tinkernorth.com/privacy/dish-windows/",
                                  "url_privacy_policy (localizable override)");
    const QString urlGithub =
        tr("https://github.com/TinkerNorth", "url_github (localizable override)");
    auto addLinkCard = [&](const QString& title, const QString& url) {
        auto* button = new QPushButton(title, body);
        button->setStyleSheet(outlinedButtonQss());
        QObject::connect(button, &QPushButton::clicked, this,
                         [this, url] { openExternalUrl(url, notifications_); });
        layout->addWidget(button, 0, Qt::AlignLeft);
    };
    layout->addWidget(makeSection(body, tr("LINKS")));
    addLinkCard(tr("Privacy policy"), urlPrivacy);
    addLinkCard(tr("View source on GitHub"), urlGithub);

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
