// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "SettingsView.h"

#include "FeatureSettings.h"
#include "Theme.h"
#include "source/store/CrashReportingStore.h"
#include "UI/settings/CrashReportingRow.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>

namespace dish::ui {

namespace {

// A tappable "row card": title + subtitle, opening some screen on click. Used by
// the Setup & Help and About sections. The click is wired by the caller.
QPushButton* makeRowButton(QWidget* parent, const QString& title, const QString& subtitle) {
    auto* button = new QPushButton(parent);
    button->setText(QStringLiteral("%1\n%2").arg(title, subtitle));
    button->setStyleSheet(
        QStringLiteral("QPushButton { text-align: left; padding: 10px 12px; color: %1; "
                       "border: 1px solid %2; border-radius: 8px; background: %3; }"
                       "QPushButton:hover { border-color: %4; }")
            .arg(hex(Theme::onSurface), hex(Theme::outline), hex(Theme::surface),
                 hex(Theme::primary)));
    return button;
}

} // namespace

SettingsView::SettingsView(FeatureSettings* settings, source::ThemePreferenceStore* themeStore,
                           source::CrashReportingStore* crashStore,
                           NotificationQueue* notifications, QWidget* parent)
    : QWidget(parent), settings_(settings), themeStore_(themeStore), crashStore_(crashStore),
      notifications_(notifications) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    // Header: title + Done.
    auto* headerRow = new QHBoxLayout;
    auto* title = new QLabel(tr("Settings"), this);
    title->setStyleSheet(
        QStringLiteral("font-size: 17px; font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* doneButton = new QPushButton(tr("Done"), this);
    QObject::connect(doneButton, &QPushButton::clicked, this, &SettingsView::closeRequested);
    headerRow->addWidget(title, 1, Qt::AlignVCenter);
    headerRow->addWidget(doneButton, 0, Qt::AlignVCenter);
    layout->addLayout(headerRow);

    // ── Setup & Help (Workstream 3a) ─────────────────────────────────────────
    auto* setupHeader = new QLabel(tr("SETUP & HELP"), this);
    setupHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(setupHeader);
    auto* wizardRow =
        makeRowButton(this, tr("Setup guide"),
                      tr("Walk through connection and controller setup. Re-run any time."));
    QObject::connect(wizardRow, &QPushButton::clicked, this, &SettingsView::setupWizardRequested);
    layout->addWidget(wizardRow);
    auto* helpRow = makeRowButton(this, tr("Help & FAQ"),
                                  tr("Concepts, performance tips, and troubleshooting."));
    QObject::connect(helpRow, &QPushButton::clicked, this, &SettingsView::helpRequested);
    layout->addWidget(helpRow);

    // ── Appearance — theme picker (Workstream 3d) ────────────────────────────
    auto* appearanceHeader = new QLabel(tr("APPEARANCE"), this);
    appearanceHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(appearanceHeader);

    auto* themeCard = new QFrame(this);
    themeCard->setObjectName(QStringLiteral("card"));
    auto* themeCol = new QVBoxLayout(themeCard);
    themeCol->setContentsMargins(14, 12, 14, 12);
    themeCol->setSpacing(8);
    auto* themeTitle = new QLabel(tr("Theme"), themeCard);
    themeTitle->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* themeDetail = new QLabel(
        tr("Choose how Dish looks. System matches your Windows light or dark setting."), themeCard);
    themeDetail->setWordWrap(true);
    themeDetail->setStyleSheet(
        QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    themeCol->addWidget(themeTitle);
    themeCol->addWidget(themeDetail);

    // Three-way segmented control (Light / Dark / System) as exclusive buttons.
    auto* chipRow = new QHBoxLayout;
    chipRow->setSpacing(6);
    themeGroup_ = new QButtonGroup(this);
    themeGroup_->setExclusive(true);
    const struct {
        source::ThemeMode mode;
    } modes[] = {
        {source::ThemeMode::Light}, {source::ThemeMode::Dark}, {source::ThemeMode::System}};
    for (const auto& m : modes) {
        auto* chip = new QPushButton(source::themeModeLabel(m.mode), themeCard);
        chip->setCheckable(true);
        chip->setCursor(Qt::PointingHandCursor);
        const int modeValue = static_cast<int>(m.mode);
        themeGroup_->addButton(chip, modeValue);
        chipRow->addWidget(chip);
    }
    chipRow->addStretch(1);
    themeCol->addLayout(chipRow);
    layout->addWidget(themeCard);

    // Reflect the store's current mode, then wire clicks. (No re-write on the
    // first frame — we set the checked button from the store value.)
    if (themeStore_ != nullptr) {
        if (auto* current = themeGroup_->button(static_cast<int>(themeStore_->mode()))) {
            current->setChecked(true);
        }
        QObject::connect(themeGroup_, &QButtonGroup::idClicked, this,
                         &SettingsView::onThemeChipClicked);
    }

    // ── Forwarded features — light bar (existing) ────────────────────────────
    auto* sectionHeader = new QLabel(tr("FORWARDED FEATURES"), this);
    sectionHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(sectionHeader);

    // Light-bar row — a card holding the title, an explanatory line and the
    // Follow game / Off picker. Mirrors dish-mac's SettingsView lightbarRow.
    auto* card = new QFrame(this);
    card->setObjectName(QStringLiteral("card"));
    auto* cardLayout = new QHBoxLayout(card);
    cardLayout->setContentsMargins(14, 12, 14, 12);
    cardLayout->setSpacing(12);

    auto* textColumn = new QVBoxLayout;
    textColumn->setSpacing(2);
    auto* rowTitle = new QLabel(tr("Light bar"), card);
    rowTitle->setStyleSheet(
        QStringLiteral("font-weight: 600; color: %1;").arg(hex(Theme::onSurface)));
    auto* rowDetail = new QLabel(tr("Follow game: the controller LED matches the host game. "
                                    "Off: leave the LED untouched."),
                                 card);
    rowDetail->setWordWrap(true);
    rowDetail->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    textColumn->addWidget(rowTitle);
    textColumn->addWidget(rowDetail);

    lightbarCombo_ = new QComboBox(card);
    // Item data carries the LightbarMode so the index→mode mapping survives a
    // future reordering of the rows.
    lightbarCombo_->addItem(lightbarModeLabel(LightbarMode::FollowGame),
                            QVariant::fromValue(static_cast<int>(LightbarMode::FollowGame)));
    lightbarCombo_->addItem(lightbarModeLabel(LightbarMode::Off),
                            QVariant::fromValue(static_cast<int>(LightbarMode::Off)));
    const int current = (settings_->lightbarMode() == LightbarMode::Off) ? 1 : 0;
    lightbarCombo_->setCurrentIndex(current);
    QObject::connect(lightbarCombo_, &QComboBox::currentIndexChanged, this,
                     &SettingsView::onLightbarModeChanged);

    cardLayout->addLayout(textColumn, 1);
    cardLayout->addWidget(lightbarCombo_, 0, Qt::AlignVCenter);
    layout->addWidget(card);

    auto* footnote = new QLabel(tr("Features only apply when your controller's hardware "
                                   "supports them — the controller list shows what was detected."),
                                this);
    footnote->setWordWrap(true);
    footnote->setStyleSheet(QStringLiteral("color: %1; font-size: 11px;").arg(hex(Theme::muted)));
    layout->addWidget(footnote);

    // Entry point to the per-device dead-zone / motion page (Workstream 2d).
    auto* controllerHeader = new QLabel(tr("CONTROLLER TUNING"), this);
    controllerHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(controllerHeader);

    auto* deadzonesButton = new QPushButton(tr("Dead zones && motion…"), this);
    QObject::connect(deadzonesButton, &QPushButton::clicked, this,
                     &SettingsView::deadzonesRequested);
    layout->addWidget(deadzonesButton, 0, Qt::AlignLeft);

    // ── Diagnostics (slot reserved for Workstream 3e) ────────────────────────
    auto* diagnosticsHeader = new QLabel(tr("DIAGNOSTICS"), this);
    diagnosticsHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(diagnosticsHeader);
    if (crashStore_ != nullptr) {
        // 3e owns the switch widget + binding; we just insert the factory's row.
        layout->addWidget(makeCrashReportingRow(crashStore_, this));
    }

    // ── About (Workstreams 3b / 3c) ──────────────────────────────────────────
    auto* aboutHeader = new QLabel(tr("ABOUT"), this);
    aboutHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(aboutHeader);
    auto* licensesRow = makeRowButton(this, tr("Open source licenses"),
                                      tr("Acknowledgements for the libraries Dish is built on."));
    QObject::connect(licensesRow, &QPushButton::clicked, this, &SettingsView::licensesRequested);
    layout->addWidget(licensesRow);
    auto* supportRow = makeRowButton(this, tr("Support Dish"),
                                     tr("Donate via GitHub Sponsors, Ko-fi, or Buy Me a Coffee."));
    QObject::connect(supportRow, &QPushButton::clicked, this, &SettingsView::donateRequested);
    layout->addWidget(supportRow);

    layout->addStretch(1);
}

void SettingsView::onLightbarModeChanged(int index) {
    const QVariant selectedData = lightbarCombo_->itemData(index);
    const auto mode = static_cast<LightbarMode>(selectedData.toInt());
    settings_->setLightbarMode(mode);
}

void SettingsView::onThemeChipClicked(int modeValue) {
    if (themeStore_ == nullptr) { return; }
    const auto mode = static_cast<source::ThemeMode>(modeValue);
    // setMode persists + republishes; the ThemeController re-themes off the
    // Observable (Source derives, Controller effects — they can't drift).
    themeStore_->setMode(mode);
}

} // namespace dish::ui
