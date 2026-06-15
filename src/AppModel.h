// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#pragma once

#include "FeatureSettings.h"
#include "Input/GamepadInputProcessor.h"
#include "Input/SDLGamepadBridge.h"
#include "Models/Models.h"
#include "Network/ConnectionHub.h"
#include "Network/ConnectionStore.h"
#include "Network/WifiConnectionManager.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"
#include "source/http/SatelliteCatalogRepository.h"
#include "source/store/ControllerTypeStore.h"
#include "Util/DisplaySleepInhibitor.h"
#include "Util/ScreenWakeController.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QTimer>

#include <memory>
#include <mutex>

namespace dish {

// Single immutable slice of state consumed by the UI. Mirrors the shape of
// dish-android's MainUiState so all three Dish clients (Android Kotlin,
// macOS Swift, Linux C++) expose one canonical state object instead of a
// fan of independent fields + signals.
struct MainUiState {
    // Named `slotList` (not `slots`) because Qt's moc treats `slots` as a
    // reserved keyword via the `Q_SLOTS` macro and won't parse a member
    // with that name.
    QList<models::ControllerSlot> slotList;
    QList<models::ConnectionSummary> connections;
    std::optional<models::DiscoveredServer> pairingTarget;
    // True while any WiFi connection is registering a controller. Drives the
    // dashboard's indeterminate spinner.
    bool busy = false;
};

// Top-level application state. Owns the network + input layers and stitches
// them together the same way the Mac AppModel and Android MainViewModel do.
//
//   * exposes a flat slot list (1 virtual + 1 per attached SDL gamepad),
//   * maintains a slotId -> WifiConnection routing table updated from the Qt
//     main thread and consulted from the SDL gamepad thread on every report,
//   * surfaces a transient errorMessage signal for one-shot toasts.
class AppModel : public QObject {
    Q_OBJECT
  public:
    // Production constructor: builds a SetThreadExecutionStateInhibitor
    // under the hood. The unique_ptr overload below lets tests inject a fake.
    explicit AppModel(QObject* parent = nullptr);
    AppModel(std::unique_ptr<util::DisplaySleepInhibitor> inhibitor, QObject* parent = nullptr);
    ~AppModel() override;

    net::ConnectionStore* store() { return store_.get(); }
    net::WifiConnectionManager* wifi() { return wifi_; }
    net::ConnectionHub* hub() { return hub_; }
    // The kernel Coordinator over the connection subsystem: it re-exposes the
    // ConnectionsComposer's derived row list and carries the bind/forget/
    // auto-reconnect commands (Workstream 2b). The hot-path binding/routing
    // still lives on hub(); this is the reactive/command surface the UI binds to.
    composer::ConnectionCoordinator* connections() { return connections_; }
    input::GamepadInputProcessor* processor() { return &processor_; }
    input::SDLGamepadBridge* bridge() { return bridge_; }
    util::ScreenWakeController* wake() { return &wake_; }
    // Feature-forwarding preferences (light bar on/off). Owned by the model;
    // the settings UI binds to it and the lightbar handlers gate on it.
    FeatureSettings* featureSettings() { return featureSettings_; }

    // ── Workstream 2c: catalog-driven "Emulate" picker ───────────────────────

    // The per-slot controller-type override store the Emulate picker writes and
    // the controllerType resolver (threaded into the descriptor PUT) reads.
    source::ControllerTypeStore* typeStore() { return &typeStore_; }

    // The pickable controller types for a slot's Emulate dialog, derived from
    // the cached catalog of the satellite the slot is bound to (empty if the
    // slot is unbound or no catalog has been fetched yet). The picker UI opens
    // with this list. Mirrors android's per-slot picker derivation.
    QList<composer::PickableType> pickableTypesFor(const QString& slotId) const;

    // The slot's current emulated type id (the user override if set, else the
    // pad's hardware classification, else Xbox). Pre-selects the picker.
    int currentTypeFor(const QString& slotId) const;

    // Apply the user's Emulate choice: write the override into the type store
    // and re-attach the slot so the new descriptor is PUT to the satellite.
    // Mirrors android ConnectionCoordinator.setControllerType.
    void setSlotControllerType(const QString& slotId, int type);

    // Kick a catalog fetch for the satellite a slot is bound to (best-effort;
    // unauthenticated GET /api/catalog with ETag revalidation). On success the
    // catalog snapshot Observable updates and the picker can render fresh types.
    void refreshCatalogForSlot(const QString& slotId);

    // Single read-only accessor — the UI reads everything off this slice
    // and re-renders on stateChanged().
    const MainUiState& state() const { return state_; }

    // The pairingTarget is a one-shot UI trigger: the dialog reads it on
    // stateChanged() and clears it before showing the prompt.
    void clearPairingTarget();

    void start();

  signals:
    // Emitted after any field of state() changes. Replaces the previous
    // slotsChanged / connectionsChanged / pairingTargetChanged trio.
    void stateChanged();

    // Transient one-shot — errors are events, not state, and are surfaced
    // as toasts/dialogs by MainWindow.
    void errorMessage(const QString& msg);

  private:
    void rebuild();
    void onHubChanged();
    void onBridgeDevicesChanged();
    void onWifiEvent(const net::ConnectionEvent& evt);
    // Walk the WifiConnectionManager pool and install our rumble handler on
    // any connection that doesn't already have one. Idempotent — invoked on
    // every poolChanged signal so newly-created connections get wired.
    void installRumbleHandlers();

    // Resolve the controller type to advertise for a slot: the user's Emulate
    // override (ControllerTypeStore) wins; absent that, the pad's SDL hardware
    // classification; absent that, Xbox. This is what bind()/attachSlot threads
    // into the descriptor PUT, so an Emulate choice reaches the satellite.
    int resolveControllerType(const QString& slotId) const;

    std::unique_ptr<net::ConnectionStore> store_;
    net::WifiConnectionManager* wifi_;
    net::ConnectionHub* hub_;
    composer::ConnectionCoordinator* connections_;
    input::GamepadInputProcessor processor_;
    input::SDLGamepadBridge* bridge_;
    FeatureSettings* featureSettings_;
    QTimer* autoReconnectTimer_;

    // Set of connection ids we've already attached rumble handlers to, so we
    // don't reinstall on every pool churn. WifiConnections live until
    // application teardown so this set never gets pruned.
    QSet<QString> rumbleWiredConnections_;
    // Owned in unique_ptr so we can swap a FakeDisplaySleepInhibitor in
    // tests. ScreenWakeController holds a raw back-pointer; lifetime is
    // tied to the AppModel.
    std::unique_ptr<util::DisplaySleepInhibitor> inhibitor_;
    util::ScreenWakeController wake_;

    MainUiState state_;

    // ── Workstream 2c: catalog + Emulate-picker state ────────────────────────
    // A dedicated HTTPClient for the unauthenticated catalog GET (kept off the
    // session path so a catalog fetch never perturbs a live connection). Owned
    // as a QObject child of this AppModel.
    net::HTTPClient* catalogHttp_;
    source::SatelliteCatalogRepository catalogRepo_;
    source::ControllerTypeStore typeStore_;
    // The currently-relevant catalog snapshot the CatalogComposer projects into
    // a pickable-type list. Updated when a catalog fetch lands.
    arch::Observable<composer::CatalogSnapshot> catalogSnapshot_;
    composer::CatalogComposer catalogComposer_;

    // slotId -> active sender. Read on the SDL gamepad thread; written on the
    // Qt main thread. Guarded by routingMtx_ for both directions.
    mutable std::mutex routingMtx_;
    QHash<QString, net::ConnectionHub::ReportSender> routing_;
    // Parallel motion + battery routes. Read on the SDL sensor / battery-
    // poll threads (both currently inside SDLGamepadBridge::runLoop), written
    // on the Qt main thread under the same routingMtx_.
    QHash<QString, net::ConnectionHub::MotionSender> motionRouting_;
    QHash<QString, net::ConnectionHub::BatterySender> batteryRouting_;
    QHash<QString, net::ConnectionHub::TouchpadSender> touchpadRouting_;
};

} // namespace dish
