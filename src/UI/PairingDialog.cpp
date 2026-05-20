// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "PairingDialog.h"

#include "AppModel.h"
#include "DishLoaders.h"
#include "Network/WifiConnection.h"
#include "Network/WifiConnectionManager.h"
#include "Theme.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

namespace dish::ui {

PairingDialog::PairingDialog(const models::DiscoveredServer& server, QWidget* parent)
    : QDialog(parent) {
    buildUi(server, nullptr);
}

PairingDialog::PairingDialog(const models::DiscoveredServer& server, AppModel* model,
                             QWidget* parent)
    : QDialog(parent) {
    buildUi(server, model);
}

void PairingDialog::buildUi(const models::DiscoveredServer& server, AppModel* model) {
    setWindowTitle(QStringLiteral("Pair with %1").arg(server.name));
    setMinimumWidth(360);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto* header = new QLabel(QStringLiteral("PAIRING"), this);
    header->setStyleSheet(sectionHeaderQss());
    layout->addWidget(header);

    auto* msg =
        new QLabel(QStringLiteral("Enter the 6-digit PIN displayed on %1").arg(server.name), this);
    msg->setWordWrap(true);
    layout->addWidget(msg);

    pinEdit_ = new QLineEdit(this);
    pinEdit_->setMaxLength(6);
    pinEdit_->setPlaceholderText(QStringLiteral("PIN"));
    pinEdit_->setInputMethodHints(Qt::ImhDigitsOnly);
    layout->addWidget(pinEdit_);

    // Cancel + Pair. Both gain the canonical 0.4 disabled-opacity treatment.
    // Pair is a `DishLoaderButton` so it can show the in-flight spinner +
    // label combo per the design spec when pairing is in flight.
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    cancelButton_ = new QPushButton(QStringLiteral("Cancel"), this);
    applyDisabledOpacityEffect(cancelButton_);
    pairButton_ = new DishLoaderButton(QStringLiteral("Pair"), this);
    pairButton_->setObjectName(QStringLiteral("primary"));
    row->addWidget(cancelButton_);
    row->addWidget(pairButton_);
    layout->addLayout(row);

    if (model == nullptr) {
        // Synchronous (legacy) mode: caller drives pairWithPin after exec().
        // Pair simply accepts the dialog; the in-flight visual is not
        // surfaced here because the dialog is gone by the time the network
        // call starts.
        QObject::connect(pairButton_, &QPushButton::clicked, this, &QDialog::accept);
        QObject::connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
        return;
    }

    // Async mode: dialog stays open, drives pairWithPin internally, mirrors
    // pairingInFlight state into the buttons, auto-dismisses on success.
    QObject::connect(pairButton_, &QPushButton::clicked, this,
                     [this, server, model] { onPairClicked(server, model); });
    QObject::connect(cancelButton_, &QPushButton::clicked, this, &QDialog::reject);
    const QString serverId = net::WifiConnection::idFor(server);
    QObject::connect(model->wifi(), &net::WifiConnectionManager::pairingInFlightChanged, this,
                     [this, serverId] { onPairingInFlightChanged(serverId); });
    // Mirror dish-mac's `if didSubmit, !nowPairing, model.errorMessage == nil
    // { dismiss() }` — an in-flight error trip suppresses the auto-dismiss so
    // the user can retry with the same PIN field rather than starting over.
    QObject::connect(model, &AppModel::errorMessage, this, [this](const QString&) {
        if (submitted_) { submissionFailed_ = true; }
    });
}

QString PairingDialog::pin() const {
    return pinEdit_ != nullptr ? pinEdit_->text().trimmed() : QString();
}

void PairingDialog::onPairClicked(const models::DiscoveredServer& server, AppModel* model) {
    submitted_ = true;
    // Reset the per-submission failure flag — a fresh attempt should not
    // inherit the previous attempt's error veto on auto-dismiss.
    submissionFailed_ = false;
    // Hand off to the WifiConnectionManager. The pairingInFlightChanged
    // signal will fire on the same loop tick — `onPairingInFlightChanged`
    // does the in-flight UI update, dismisses on success, restores the
    // buttons on error. Cancel is locked through the in-flight window.
    model->wifi()->pairWithPin(server, pin());
}

void PairingDialog::onPairingInFlightChanged(const QString& serverId) {
    // The signal's sender is the WifiConnectionManager that emitted
    // pairingInFlightChanged. Pull the canonical in-flight state off it
    // (rather than caching a separate copy) so this is always a single
    // source of truth.
    auto* wifi = qobject_cast<net::WifiConnectionManager*>(sender());
    if (wifi == nullptr) { return; }
    const bool pairing = wifi->isPairingInFlight(serverId);
    pairButton_->setInFlight(pairing, QStringLiteral("Pairing…"));
    // Cancel and the PIN field follow the same disabled state — typing a
    // new PIN mid-request would race the response we're already waiting on.
    cancelButton_->setEnabled(!pairing);
    pinEdit_->setEnabled(!pairing);

    // Mirror dish-mac's `.onChange(of: isPairing)`: once a submission was
    // made and pairing is no longer in flight, dismiss the dialog only if no
    // error was raised in the meantime. The pairingInFlight set is cleared
    // in every terminal branch of WifiConnectionManager (success,
    // AuthRequired, Unreachable, openSession failure / live), so this fires
    // exactly once per submission. On the error path the dialog stays open
    // so the user can retry with a fresh PIN entry — the error itself has
    // already surfaced as a toast/QMessageBox via AppModel::errorMessage.
    if (submitted_ && !pairing) {
        if (!submissionFailed_) { accept(); }
        submitted_ = false;
    }
}

} // namespace dish::ui
