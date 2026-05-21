// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "Models/Models.h"

#include <QDialog>

class QLineEdit;
class QPushButton;

namespace dish {
class AppModel;
}

namespace dish::ui {

class DishLoaderButton;

// Modal sheet shown when the satellite server requires a fresh pairing PIN.
// Mirrors dish-mac/UI/PairingSheet.swift.
//
// Two construction modes:
//   * Legacy (`server`, `parent`): synchronous — exec(), accept on Pair,
//     caller fires the pair request afterwards. Kept for MainWindow's pending
//     pairingTarget flow, which already runs pairWithPin itself.
//   * Async (`server`, `model`, `parent`): the Pair button drives the
//     pairing flow directly and shows the in-flight spinner-plus-disabled
//     state. The dialog stays open until pairing succeeds (auto-dismiss
//     when the connection completes) or fails (banner shown next to the
//     PIN field). Cancel is also disabled while pairing is in flight.
class PairingDialog : public QDialog {
    Q_OBJECT
  public:
    PairingDialog(const models::DiscoveredServer& server, QWidget* parent = nullptr);
    PairingDialog(const models::DiscoveredServer& server, AppModel* model,
                  QWidget* parent = nullptr);
    QString pin() const;

  private:
    // Shared layout builder. `model` is non-null only in async mode and
    // enables the in-flight wiring of the Pair / Cancel buttons.
    void buildUi(const models::DiscoveredServer& server, AppModel* model);
    // Slot bound to wifi->pairingInFlightChanged in async mode. Mirrors the
    // SwiftUI .onChange(of: isPairing) in dish-mac/UI/PairingSheet.swift.
    void onPairingInFlightChanged(const QString& serverId);
    void onPairClicked(const models::DiscoveredServer& server, AppModel* model);

    QLineEdit* pinEdit_ = nullptr;
    DishLoaderButton* pairButton_ = nullptr;
    QPushButton* cancelButton_ = nullptr;
    // True between onPairClicked and the next pairingInFlightChanged where
    // our serverId leaves the in-flight set. Drives the auto-dismiss
    // semantics.
    bool submitted_ = false;
    // Set true if AppModel::errorMessage fires while a submission is in
    // flight. When set, the dialog does NOT auto-dismiss on clearance — the
    // user gets to retry with the same dialog still open (after the modal
    // error dialog from MainWindow is acknowledged). Mirrors dish-mac's
    // PairingSheet.swift behaviour where the sheet only dismisses iff
    // `model.errorMessage == nil`.
    bool submissionFailed_ = false;
};

} // namespace dish::ui
