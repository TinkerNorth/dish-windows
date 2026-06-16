// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// AppViewModel — the single QML-facing exposure object over dish::AppModel. It
// is a THIN adapter: it owns no UI state of its own beyond a cache of the values
// it re-publishes as NOTIFY properties, and every command forwards verbatim to
// the existing AppModel / WifiConnectionManager / ConnectionCoordinator. All
// derivation (status text, connection rows, slot live-stats) already happens in
// the C++ stores/composers; this class only maps them to Q_PROPERTY + role
// models and re-emits change signals so QML bindings stream.
//
// Registered as a context property by QmlEntryPoint (the model outlives the
// engine). Lives in dish_core so its mapping helpers are unit-testable without
// the Quick/Qml stack; the QML registration is done in the Quick target.

#pragma once

#include "Models/Models.h"
#include "qml/ConnectionListModel.h"
#include "qml/SlotListModel.h"

#include <QObject>
#include <QString>

class QTimer;

namespace dish {
class AppModel;
}

namespace dish::qml {

class AppViewModel : public QObject {
    Q_OBJECT

    // ── Dashboard header (mirrors MainWindow::rebuildHeader) ─────────────────
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)
    Q_PROPERTY(QString summaryText READ summaryText NOTIFY stateChanged)
    Q_PROPERTY(int onlineCount READ onlineCount NOTIFY stateChanged)
    Q_PROPERTY(int connectionCount READ connectionCount NOTIFY stateChanged)
    // True while any connection is registering a controller (the dashboard's
    // indeterminate spinner). Mirrors MainUiState::busy.
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)

    // ── Live telemetry footer (mirrors MainWindow::onTelemetryTick) ──────────
    // Sampled ~1 Hz off the processor; the bindings stream as the numbers move.
    Q_PROPERTY(int eventsPerSec READ eventsPerSec NOTIFY telemetryChanged)
    Q_PROPERTY(int sendsPerSec READ sendsPerSec NOTIFY telemetryChanged)
    Q_PROPERTY(qulonglong totalSent READ totalSent NOTIFY telemetryChanged)

    // ── Pairing one-shot (mirrors MainWindow::showPairingPrompt) ─────────────
    // pairingActive flips true when the AppModel parks a pairingTarget; the QML
    // pairing sheet opens on it and calls clearPairingTarget() before showing.
    Q_PROPERTY(bool pairingActive READ pairingActive NOTIFY stateChanged)
    Q_PROPERTY(QString pairingServerName READ pairingServerName NOTIFY stateChanged)

    // ── Collections the page agents iterate ──────────────────────────────────
    // The slot/controller model (a SlotCard per row) and the connection-row
    // model (a ConnectionsDialog row per row). Both are owned children; the
    // pointers are stable for the app lifetime so QML can bind once. NOT named
    // `slots` because moc strips that token (the Q_SLOTS keyword), the same trap
    // MainUiState::slotList avoids.
    Q_PROPERTY(dish::qml::SlotListModel* slotModel READ slotModel CONSTANT)
    Q_PROPERTY(dish::qml::ConnectionListModel* connectionModel READ connectionModel CONSTANT)

  public:
    explicit AppViewModel(dish::AppModel* model, QObject* parent = nullptr);

    QString statusText() const { return statusText_; }
    QString summaryText() const { return summaryText_; }
    int onlineCount() const { return onlineCount_; }
    int connectionCount() const { return connectionCount_; }
    bool busy() const { return busy_; }

    int eventsPerSec() const { return eventsPerSec_; }
    int sendsPerSec() const { return sendsPerSec_; }
    qulonglong totalSent() const { return totalSent_; }

    bool pairingActive() const { return pairingActive_; }
    QString pairingServerName() const { return pairingServerName_; }

    SlotListModel* slotModel() { return &slotModel_; }
    ConnectionListModel* connectionModel() { return &connectionModel_; }

    // ── Commands (forward verbatim to the existing AppModel surface) ─────────

    // Bind / unbind a slot to a connection (the SlotCard bind menu / Unbind).
    Q_INVOKABLE void bindSlot(const QString& slotId, const QString& connectionId);
    Q_INVOKABLE void unbindSlot(const QString& slotId);

    // Emulate picker: kick a catalog refresh, then read the offerable types +
    // the slot's current type. emulateTypes returns a list of JS objects
    // {type,slug,name,shortName,description,known}; emulateCurrentType the
    // pre-selected wire id. setControllerType applies the choice.
    Q_INVOKABLE void refreshEmulate(const QString& slotId);
    Q_INVOKABLE QVariantList emulateTypes(const QString& slotId) const;
    Q_INVOKABLE int emulateCurrentType(const QString& slotId) const;
    Q_INVOKABLE void setControllerType(const QString& slotId, int type);

    // Connections page: discovery + connect + forget. discoveredServers returns
    // {name,ip,udpPort,pairPort,httpPort,machineId,id} objects for the FOUND
    // list (the rows that aren't yet remembered surface only here).
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE bool isScanning() const;
    Q_INVOKABLE QVariantList discoveredServers() const;
    Q_INVOKABLE void connectByIndex(int discoveredIndex);
    Q_INVOKABLE void forgetConnection(const QString& connectionId);

    // Pairing sheet: submit a PIN for a discovered server (by its index in
    // discoveredServers), query the in-flight state, and clear the one-shot
    // pairing trigger before showing the sheet.
    Q_INVOKABLE void pairWithPin(int discoveredIndex, const QString& pin);
    Q_INVOKABLE bool isPairingInFlight(const QString& serverId) const;
    Q_INVOKABLE void clearPairingTarget();

  signals:
    // Any header/slot/connection/pairing state changed (folds AppModel's
    // stateChanged + the coordinator's connectionsChanged).
    void stateChanged();
    // Telemetry footer numbers moved (the ~1 Hz sample).
    void telemetryChanged();
    // Transient one-shot error, forwarded from AppModel::errorMessage so the QML
    // toast host can surface it.
    void errorMessage(const QString& message);

  private:
    // Recompute the cached header/pairing fields + repush the slot model from
    // the AppModel's current state slice, then emit stateChanged().
    void onStateChanged();
    // Repush the connection model from the coordinator's derived rows.
    void onConnectionsChanged();
    // Sample the processor telemetry (the same drain MainWindow does).
    void onTelemetryTick();

    dish::AppModel* model_;
    SlotListModel slotModel_;
    ConnectionListModel connectionModel_;
    QTimer* telemetryTimer_;

    QString statusText_;
    QString summaryText_;
    int onlineCount_ = 0;
    int connectionCount_ = 0;
    bool busy_ = false;

    int eventsPerSec_ = 0;
    int sendsPerSec_ = 0;
    qulonglong totalSent_ = 0;

    bool pairingActive_ = false;
    QString pairingServerName_;
};

} // namespace dish::qml
