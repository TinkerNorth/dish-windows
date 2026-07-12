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

namespace dish::models {
struct RememberedWifi;
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
    // Connect to the selected REMEMBERED satellite using its last-known endpoint
    // (no rescan required). The paired key persists, so no PIN is needed; if the
    // box moved, the manager's relearn (discovery refresh + backoff) re-resolves
    // the address. Android parity: ConnectionsActivity connects a Known row.
    void onReconnectClicked();
    void onForgetClicked();
    // Refresh the in-flight state of the Scan + Connect buttons. Pulled out
    // of the constructor so any of `scanningChanged`,
    // `pairingInFlightChanged`, list-selection change, or `discoveredChanged`
    // can drive a single update path. Determining "is the current Connect
    // target pairing in-flight" lives here.
    void refreshActionState();
    // One remembered row's display text: "<name> • <ip>[ • online[ · ~3.4 ms]]".
    // The latency readout (median heartbeat-RTT/2) rides the online tag while
    // the session is live and its RTT window has samples.
    QString rememberedRowText(const models::RememberedWifi& r) const;
    // Patch remembered-row texts IN PLACE on the 1 s latency tick — a full
    // rebuildLists would clear the user's selection every second.
    void refreshRememberedTexts();

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
    // Connect to a remembered (possibly not-currently-discovered) satellite by
    // its last-known endpoint. Enabled only when a remembered row is selected
    // and it isn't already live.
    QPushButton* reconnectButton_;
    QLabel* statusLabel_;
};

} // namespace dish::ui
