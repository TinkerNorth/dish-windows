// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "ConnectionsDialog.h"

#include "AppModel.h"
#include "DishLoaders.h"
#include "Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QVBoxLayout>

namespace dish::ui {

ConnectionsDialog::ConnectionsDialog(AppModel* model, QWidget* parent)
    : QDialog(parent), model_(model) {
    setWindowTitle(QStringLiteral("Connections"));
    setMinimumSize(560, 420);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(14);

    auto* discoveredHeader = new QLabel(QStringLiteral("FOUND"), this);
    discoveredHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(discoveredHeader);

    discoveredList_ = new QListWidget(this);
    layout->addWidget(discoveredList_, 1);

    auto* row = new QHBoxLayout;
    scanButton_ = new DishLoaderButton(QStringLiteral("Scan"), this);
    statusLabel_ = new QLabel(this);
    statusLabel_->setStyleSheet(QStringLiteral("color: %1;").arg(hex(Theme::muted)));
    connectButton_ = new DishLoaderButton(QStringLiteral("Connect"), this);
    connectButton_->setObjectName(QStringLiteral("primary"));
    row->addWidget(scanButton_);
    row->addWidget(statusLabel_, 1);
    row->addWidget(connectButton_);
    layout->addLayout(row);

    auto* rememberedHeader = new QLabel(QStringLiteral("REMEMBERED"), this);
    rememberedHeader->setStyleSheet(sectionHeaderQss());
    layout->addWidget(rememberedHeader);

    rememberedList_ = new QListWidget(this);
    layout->addWidget(rememberedList_, 1);

    auto* forgetBtn = new QPushButton(QStringLiteral("Forget"), this);
    // Forget is local + instant, so it does not get the spinner treatment.
    // It still gets the canonical 0.4 disabled-alpha rule for consistency.
    applyDisabledOpacityEffect(forgetBtn);
    auto* row2 = new QHBoxLayout;
    row2->addStretch(1);
    row2->addWidget(forgetBtn);
    layout->addLayout(row2);

    QObject::connect(scanButton_, &QPushButton::clicked, this, &ConnectionsDialog::onScanClicked);
    QObject::connect(connectButton_, &QPushButton::clicked, this,
                     &ConnectionsDialog::onConnectClicked);
    QObject::connect(forgetBtn, &QPushButton::clicked, this, &ConnectionsDialog::onForgetClicked);

    QObject::connect(model_->wifi(), &net::WifiConnectionManager::discoveredChanged, this,
                     &ConnectionsDialog::rebuildLists);
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::scanningChanged, this,
                     &ConnectionsDialog::refreshActionState);
    // Selection in the discovered list changes which server the Connect
    // button targets \u2014 and therefore which pairingInFlight entry it should
    // mirror. Re-evaluate every selection change.
    QObject::connect(discoveredList_, &QListWidget::itemSelectionChanged, this,
                     &ConnectionsDialog::refreshActionState);
    QObject::connect(model_->wifi(), &net::WifiConnectionManager::pairingInFlightChanged, this,
                     &ConnectionsDialog::refreshActionState);
    QObject::connect(model_->hub(), &net::ConnectionHub::changed, this,
                     &ConnectionsDialog::rebuildLists);

    rebuildLists();
    refreshActionState();
}

void ConnectionsDialog::refreshActionState() {
    // Scan: spinner + "Scanning..." while WifiConnectionManager has a
    // discovery future in flight. Button is force-disabled by setInFlight.
    const bool scanning = model_->wifi()->isScanning();
    scanButton_->setInFlight(scanning, QStringLiteral("Scanning\u2026"));
    statusLabel_->setText(scanning ? QStringLiteral("Scanning\u2026") : QString());

    // Connect: spinner + "Pairing..." for the currently-selected discovered
    // server iff its pair is in flight. Selecting a different row swaps the
    // target; if no row is selected, the button is disabled but not in
    // flight (idle disabled).
    auto* item = discoveredList_->currentItem();
    if (item == nullptr) {
        connectButton_->setInFlight(false);
        connectButton_->setIdleEnabled(false);
        return;
    }
    const auto selectedId = item->data(Qt::UserRole).toString();
    const bool pairing = model_->wifi()->isPairingInFlight(selectedId);
    connectButton_->setInFlight(pairing, QStringLiteral("Pairing\u2026"));
    // Keep the idle-enabled state truthful: a paired-in-flight button is
    // already force-disabled by setInFlight, but on completion the button
    // should re-enable iff there is a selection. Connect remains a no-op
    // if the row vanished between click and refresh.
    connectButton_->setIdleEnabled(true);
}

void ConnectionsDialog::rebuildLists() {
    discoveredList_->clear();
    for (const auto& s : model_->wifi()->discoveredServers()) {
        auto* item = new QListWidgetItem(QStringLiteral("%1 \u2022 %2 \u2022 %3")
                                             .arg(s.name.isEmpty() ? s.ip : s.name, s.ip,
                                                  models::discoverySourceLabel(s.source)));
        item->setData(Qt::UserRole, QVariant::fromValue(s.id()));
        discoveredList_->addItem(item);
    }
    rememberedList_->clear();
    for (const auto& r : model_->wifi()->remembered()) {
        auto* conn = model_->wifi()->get(r.id);
        const QString liveTag = (conn != nullptr && conn->state() == net::SessionState::Live)
                                    ? QStringLiteral(" \u2022 online")
                                    : QString();
        auto* item = new QListWidgetItem(
            QStringLiteral("%1 \u2022 %2%3").arg(r.name.isEmpty() ? r.ip : r.name, r.ip, liveTag));
        item->setData(Qt::UserRole, r.id);
        rememberedList_->addItem(item);
    }
    // Clearing the lists drops the selection \u2014 Connect/Pair targets the
    // selected row, so its idle-disabled state needs a refresh as well.
    refreshActionState();
}

void ConnectionsDialog::onScanClicked() { model_->wifi()->startDiscovery(); }

void ConnectionsDialog::onConnectClicked() {
    auto* item = discoveredList_->currentItem();
    if (item == nullptr) { return; }
    const auto wantedId = item->data(Qt::UserRole).toString();
    for (const auto& s : model_->wifi()->discoveredServers()) {
        if (s.id() == wantedId) {
            model_->wifi()->connectTo(s);
            return;
        }
    }
}

void ConnectionsDialog::onForgetClicked() {
    auto* item = rememberedList_->currentItem();
    if (item == nullptr) { return; }
    model_->wifi()->forget(item->data(Qt::UserRole).toString());
}

} // namespace dish::ui
