// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include <QList>
#include <QMainWindow>

class QLabel;
class QProgressBar;
class QPushButton;
class QTimer;
class QVBoxLayout;

namespace dish {
class AppModel;
}

namespace dish::ui {

class NotificationQueue;
class NotificationToastHost;

// Dashboard window — mirrors dish-mac MainView and dish-android activity_main.
// Status header, controllers section (one SlotCard per slot), telemetry
// footer, and a "Manage" button that opens ConnectionsDialog.
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(AppModel* model, QWidget* parent = nullptr);

  private:
    void onStateChanged();
    void rebuildHeader();
    void rebuildSlotList();
    void showPairingPrompt();
    void onTelemetryTick();
    void onManageClicked();
    void onSettingsClicked();
    // Open the per-device dead-zone / motion settings page (Workstream 2d).
    void onDeadzonesClicked();
    void onBindRequested(const QString& slotId, const QString& connectionId);
    void onUnbindRequested(const QString& slotId);
    // Open the catalog-driven Emulate picker for a bound slot (Workstream 2c).
    void onEmulateRequested(const QString& slotId);

    AppModel* model_;

    QLabel* statusDot_;
    QLabel* statusText_;
    QLabel* summaryText_;
    QPushButton* settingsButton_;
    QPushButton* manageButton_;
    // Indeterminate bar shown while a controller registration is in flight
    // (state().busy). Replaces the old ~2 s synchronous UI freeze with a
    // visible "working" cue.
    QProgressBar* dashboardSpinner_;
    QVBoxLayout* slotsLayout_;
    QLabel* slotsEmpty_;
    QLabel* telemetryLeft_;
    QLabel* telemetryRight_;

    QTimer* telemetryTimer_;
    quint64 telemetryTotal_ = 0;

    // Owned by `this` (Qt parent semantics). The queue is the typed
    // replacement for the legacy single-banner errorMessage signal — every
    // emitted error toast goes through it. The host is the bottom-anchored
    // strip that renders the stack. Both are wired by MainWindow's ctor.
    NotificationQueue* notifications_ = nullptr;
    NotificationToastHost* toastHost_ = nullptr;
};

} // namespace dish::ui
