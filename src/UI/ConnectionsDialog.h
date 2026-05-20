// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QDialog>

class QListWidget;
class QPushButton;
class QLabel;

namespace dish {
class AppModel;
}

namespace dish::ui {

class DishLoaderButton;

// "Manage connections" sheet — discovery, connect, forget. Mirrors the Mac
// ConnectionsView.
class ConnectionsDialog : public QDialog {
    Q_OBJECT
  public:
    ConnectionsDialog(AppModel* model, QWidget* parent = nullptr);

  private:
    void rebuildLists();
    void onScanClicked();
    void onConnectClicked();
    void onForgetClicked();
    // Refresh the in-flight state of the Scan + Connect buttons. Pulled out
    // of the constructor so any of `scanningChanged`,
    // `pairingInFlightChanged`, list-selection change, or `discoveredChanged`
    // can drive a single update path. Determining "is the current Connect
    // target pairing in-flight" lives here.
    void refreshActionState();

    AppModel* model_;
    QListWidget* discoveredList_;
    QListWidget* rememberedList_;
    // Scan + Connect both gain an in-flight visual (spinner + label, disabled
    // until the network round-trip completes). They are loader-aware buttons
    // rather than plain QPushButtons so the canonical Dish "0.4 opacity
    // disabled + inline spinner" pattern applies. Forget is plain (its action
    // is local and synchronous).
    DishLoaderButton* scanButton_;
    DishLoaderButton* connectButton_;
    QLabel* statusLabel_;
};

} // namespace dish::ui
