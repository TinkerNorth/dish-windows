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
#include "architecture/Observable.h"
#include "composer/CatalogComposer.h"
#include "composer/ConnectionCoordinator.h"
#include "composer/CrashReportingBackend.h"
#include "composer/CrashReportingController.h"
#include "composer/ThemeController.h"
#include "composer/WakeStateComposer.h"
#include "composer/WakeStateController.h"
#include "repository/DeadzoneRepository.h"
#include "repository/MotionPreferenceRepository.h"
#include "core/reducer/PollRateSampler.h"
#include "source/inputrate/InputRateStore.h"
#include "source/http/SatelliteCatalogRepository.h"
#include "source/store/ControllerTypeStore.h"
#include "source/store/CrashReportingStore.h"
#include "source/store/JoystickRemapStore.h"
#include "source/store/MotionEnabledStore.h"
#include "source/store/OnboardingPreferenceStore.h"
#include "source/store/ThemePreferenceStore.h"
#include "source/store/UsbPathPreferenceStore.h"
#include "source/usb/UsbGamepadManager.h"
#include "source/usb/WinHidGateway.h"
#include "Util/DisplaySleepInhibitor.h"

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
    composer::WakeStateController* wake() { return &wakeController_; }
    // Observable count of streaming slots holding the display awake (>0 == the
    // inhibitor is held). The QML header pill subscribes; read-only.
    arch::Observable<int>& keepAwakeCount() { return shouldKeepScreenOn_; }
    // Feature-forwarding preferences (light bar on/off). Owned by the model;
    // the settings UI binds to it and the lightbar handlers gate on it.
    FeatureSettings* featureSettings() { return featureSettings_; }

    // ── Workstream 3a/3d/3e: onboarding + theme + crash-reporting stores ──────

    // The first-run "welcome seen" store (Workstream 3a). The composition root
    // gates the welcome pager on !welcomeCompleted via maybeShowOnboarding().
    source::OnboardingPreferenceStore* onboardingStore() { return &onboardingStore_; }

    // The theme-mode store (Workstream 3d). The settings picker binds to it; the
    // ThemeController re-themes off its Observable.
    source::ThemePreferenceStore* themeStore() { return &themeStore_; }

    // The crash-reporting opt-out store (Workstream 3e). The Diagnostics toggle
    // binds to it; the CrashReportingController forwards flips to the backend.
    source::CrashReportingStore* crashStore() { return &crashStore_; }

    // ── Workstream 2d: deadzones + motion enable/negotiation ─────────────────

    // The durable per-device deadzone store the deadzone settings page writes
    // and the device-attach path reads (pushes into the processor once).
    repository::DeadzoneRepository* deadzoneRepository() { return &deadzoneRepo_; }

    // The per-slot motion-enable store (default on). The settings toggle writes
    // it; the CAP_MOTION negotiation + the motion routing read it.
    source::MotionEnabledStore* motionEnabledStore() { return &motionEnabledStore_; }

    // ── Raw-joystick remap ("Configure controls" page, android parity) ───────
    // The per-(vid,pid) remap store the page writes; AppModel pushes every saved
    // remap into the bridge so a generic pad decodes under its corrected layout.
    source::JoystickRemapStore* joystickRemapStore() { return &joystickRemapStore_; }

    // The effective remap for a model: the stored override if any, else the
    // default JoystickRemap (today's layout). What the remap page renders.
    input::JoystickRemap remapFor(int vendorId, int productId) const;

    // Persist a model's remap (the store republishes → pushed into the bridge so
    // it takes effect live) and clear it (revert to the default layout).
    void setJoystickRemap(int vendorId, int productId, const input::JoystickRemap& remap);
    void clearJoystickRemap(int vendorId, int productId);

    // Toggle the bridge's input-capture mode (the "press to assign" detector).
    // While on, the bridge emits rawJoystickInput which AppModel re-emits.
    void setInputCaptureEnabled(bool enabled);

    // The currently-attached controllers in (id, name, hasGyro) form, for the
    // deadzone settings page to render a per-device card.
    QList<input::SDLGamepadBridge::Device> attachedDevices() const { return bridge_->devices(); }

    // Push a freshly-chosen deadzone profile into the live processor for a
    // device — the settings page calls this on a slider change so the hot path
    // picks it up without a re-attach. Runs on the Qt main thread (the seam the
    // SDL bridge also uses at device-add); never per input event.
    void applyDeadzones(const QString& deviceId, const input::deadzone::Deadzones& dz);

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
    // bound satellite catalog's first offered type, else Xbox). Pre-selects the picker.
    int currentTypeFor(const QString& slotId) const;

    // Apply the user's Emulate choice: write the override into the type store
    // and re-attach the slot so the new descriptor is PUT to the satellite.
    // Mirrors android ConnectionCoordinator.setControllerType.
    void setSlotControllerType(const QString& slotId, int type);

    // Kick a catalog fetch for the satellite a slot is bound to (best-effort;
    // unauthenticated GET /api/catalog with ETag revalidation). On success the
    // catalog snapshot Observable updates and the picker can render fresh types.
    // Drives catalogState() through Loading → Success/Error and emits
    // catalogStateChanged() at each transition.
    void refreshCatalogForSlot(const QString& slotId);

    // The catalog fetch lifecycle for the most-recently-refreshed slot's
    // satellite: Idle / Loading / Success(possibly stale) / Error(reason). The
    // Emulate picker binds this to show a spinner while loading, the cause + a
    // retry on failure, and an empty-vs-content distinction — replacing the old
    // "the fetch silently returned nothing" behaviour. Read on the main thread.
    const source::CatalogState& catalogState() const { return catalogState_; }

    // Single read-only accessor — the UI reads everything off this slice
    // and re-renders on stateChanged().
    const MainUiState& state() const { return state_; }

    // The pairingTarget is a one-shot UI trigger: the dialog reads it on
    // stateChanged() and clears it before showing the prompt.
    void clearPairingTarget();

    void start();

    // The USB-direct claim driver (raw-HID). Exposed read-only for the settings
    // surface / tests; the lifecycle is owned here. Null only in the degenerate
    // case where the gateway failed to construct (never on Windows).
    source::usb::UsbGamepadManager* usbManager() { return usbManager_.get(); }

  signals:
    // Emitted after any field of state() changes. Replaces the previous
    // slotsChanged / connectionsChanged / pairingTargetChanged trio.
    void stateChanged();

    // Transient one-shot — errors are events, not state, and are surfaced
    // as toasts/dialogs by MainWindow.
    void errorMessage(const QString& msg);

    // The catalog fetch lifecycle (catalogState()) moved — Loading started or a
    // Success/Error landed. The QML facade folds this into an emulate-state
    // NOTIFY so the picker re-reads loading/error/stale.
    void catalogStateChanged();

    // Re-emit of the bridge's rawJoystickInput (a raw input observed during a
    // capture). AppViewModel maps deviceId → slotId and re-emits only for the
    // capturing slot. `kind` 0=axis/1=button/2=hat; `index` the raw source;
    // `value` the axis int16 / 1 for a button / SDL_HAT_* bitmask for a hat.
    void rawJoystickInput(const QString& deviceId, int kind, int index, int value);

  private:
    void rebuild();
    void onHubChanged();
    void onBridgeDevicesChanged();
    void onWifiEvent(const net::ConnectionEvent& evt);
    // Map a USB-path FSM banner reason to a localized one-shot toast (errorMessage).
    // Called from UsbObserver on the Qt main thread (the only FSM-mutating thread).
    void onUsbNotice(const reducer::UsbController& c, reducer::UsbNotice notice);
    // Re-scan the raw-HID bus, drive each present pad toward its resolved path,
    // and sample the per-device poll rate. Driven off usbScanTimer_ (and once at
    // start()). Runs on the Qt main thread — the only thread that mutates the FSM.
    void pollUsbDirect();
    // Recompute the twin-dedup suppression set + the synthetic slot list from the
    // current SDL devices × USB-direct controllers and rebuild. Invoked when a
    // synthetic is added/removed (the UsbDirectObserver) or SDL devices change.
    void onUsbDirectChanged();
    // Diff the SDL device list against the last-seen set and feed framework
    // up/down per VID:PID into the USB FSM, so a claim-failure / Standard pick can
    // settle on the live SDL device (the "framework" path on Windows).
    void syncFrameworkPresence();
    // Drive a FrameworkUp for any controller still parked in AwaitingFramework
    // whose framework (SDL twin) device is present in the live device list, so the
    // FSM settles to Standard. Needed because on Windows the twin never leaves
    // bridge_->devices() across a Direct claim, so syncFrameworkPresence's
    // appearance-edge diff alone never re-fires after a Direct->Standard release.
    // Idempotent (a settled controller is Routed, not AwaitingFramework); invoked
    // deferred (queued) from onUsbDirectChanged to avoid re-entering the FSM mid
    // effect-dispatch.
    void settleAwaitingFrameworkControllers();
    // Push every saved per-(vid,pid) remap from the store into the bridge. Called
    // on construction, on a store republish, and on a device-add (so a freshly-
    // attached pad with a saved profile decodes correctly). Idempotent; off the
    // hot path (the bridge copies the small remap under its own lock per report).
    void pushJoystickRemapsToBridge();

    // Walk the WifiConnectionManager pool and install our rumble handler on
    // any connection that doesn't already have one. Idempotent — invoked on
    // every poolChanged signal so newly-created connections get wired.
    void installRumbleHandlers();

    // Reconcile the InputRateStore's tracked slot set with the current slot list
    // (add freshly-appeared slots, drop departed ones so a tracker re-baselines
    // on re-attach). Cheap + idempotent; called from rebuild().
    void syncInputRateDevices();
    // Fold the InputRateStore's latest per-slot Hz emission into liveRatesBySlot_
    // and patch state_.slotList in place, emitting stateChanged() only when a
    // visible number actually moved (so a quiet 1 Hz tick doesn't thrash the UI).
    void onInputRatesChanged(const source::SlotInputRatesMap& rates);

    // Resolve the controller type to advertise for a slot: the user's Emulate
    // override (ControllerTypeStore) wins; absent that, the bound satellite
    // catalog's first offered type; absent that, Xbox. This is what bind()/
    // attachSlot threads into the descriptor PUT, so an Emulate choice reaches
    // the satellite.
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
    // tests. The WakeStateController holds a raw back-pointer; lifetime is
    // tied to the AppModel.
    std::unique_ptr<util::DisplaySleepInhibitor> inhibitor_;
    // Wake subsystem, kernel-split: AppModel owns the two upstream Observables
    // (the streaming-slot count derived from bindings x link states, and a
    // keep-screen-on override count), the Composer that folds them into a
    // WakeState, and the Controller that effects SetThreadExecutionState off it.
    // Declaration order matters: the Composer captures the Observables and the
    // Controller captures the Composer's state, so they must precede it.
    arch::Observable<int> streamingSlotCount_{0};
    arch::Observable<int> shouldKeepScreenOn_{0};
    composer::WakeStateComposer wakeComposer_;
    composer::WakeStateController wakeController_;

    // ── Workstream 3a/3d/3e: onboarding + theme + crash-reporting ────────────
    // Declaration order matters: each controller captures its store's Observable,
    // so the stores must precede the controllers. The crash backend (a no-op
    // seam this wave, D4) precedes its controller. The theme controller re-themes
    // the live QApplication off the theme store's ThemeMode Observable.
    source::OnboardingPreferenceStore onboardingStore_;
    source::ThemePreferenceStore themeStore_;
    source::CrashReportingStore crashStore_;
    composer::NoopCrashReportingBackend crashBackend_;
    composer::ThemeController themeController_;
    composer::CrashReportingController crashController_;

    MainUiState state_;

    // ── Workstream 2c: catalog + Emulate-picker state ────────────────────────
    // A dedicated HTTPClient for the unauthenticated catalog GET (kept off the
    // session path so a catalog fetch never perturbs a live connection). Owned
    // as a QObject child of this AppModel.
    net::HTTPClient* catalogHttp_;
    source::SatelliteCatalogRepository catalogRepo_;
    source::ControllerTypeStore typeStore_;

    // ── Workstream 2d: deadzone + motion stores ──────────────────────────────
    // Declared (and therefore constructed) in this order: the motion-preference
    // repo must exist before the MotionEnabledStore that hydrates from it.
    repository::DeadzoneRepository deadzoneRepo_;
    repository::MotionPreferenceRepository motionPrefRepo_;
    source::MotionEnabledStore motionEnabledStore_;

    // ── Raw-joystick remap store (android parity) ────────────────────────────
    // Declaration order: the repo must precede the store that hydrates from it.
    // joystickRemapSub_ folds every store republish into a full push of the saved
    // remaps to the bridge (the StateSource carries the whole map per emit, so a
    // re-push is the simplest correct thing — it is rare, off the hot path).
    source::JoystickRemapRepository joystickRemapRepo_;
    source::JoystickRemapStore joystickRemapStore_;
    arch::Observable<source::JoystickRemapMap>::Subscription joystickRemapSub_;
    // Device ids whose persisted deadzone profile we've already pushed into the
    // processor, so onBridgeDevicesChanged pushes once per attach, not per tick.
    QSet<QString> deadzonePushedDevices_;
    // The currently-relevant catalog snapshot the CatalogComposer projects into
    // a pickable-type list. Updated when a catalog fetch lands.
    arch::Observable<composer::CatalogSnapshot> catalogSnapshot_;
    composer::CatalogComposer catalogComposer_;
    // The catalog fetch lifecycle (Idle/Loading/Success/Error). A plain member
    // (not an Observable) because CatalogDto has no operator==; it is read by the
    // QML facade on catalogStateChanged() and drives the picker's loading/error
    // UX. Holds the last good catalog (stale) across a refresh/failure.
    source::CatalogState catalogState_;

    // ── Workstream 2g: USB-direct (raw-HID) claim path ──────────────────────
    // The Windows raw-HID gateway (SetupAPI/hid.dll), the per-VID:PID path-choice
    // store, the claim driver (the only USB-IO place), and the scan/poll-rate
    // timer. Declaration order: the gateway + store must precede the manager that
    // borrows them. usbObserver_ bridges the FSM's non-input effects (a synthetic
    // appeared/vanished) into a slot-list + suppression recompute.
    //
    // A synthetic USB-direct device publishes decoded reports into the SAME
    // processor_ as the SDL path (keyed by its model id string); AppModel adds it
    // to the slot list so it binds + routes through the existing hub machinery,
    // and suppresses the SDL twin so the pad streams via exactly one path.
    class UsbObserver : public source::usb::UsbDirectObserver {
      public:
        explicit UsbObserver(AppModel* owner) : owner_(owner) {}
        // Rebuild the slot list on the single "FSM state moved" signal — covers
        // EVERY transition (synthetic add/remove AND held-synthetic phase changes
        // like Direct->AwaitingFramework that the granular callbacks missed).
        void controllersChanged() override { owner_->onUsbDirectChanged(); }

        // Surface the FSM's user-facing banner instead of dropping it. The
        // persistent error STATE (RestoreStuck/NeedsReplug phase + DirectClaimFailure)
        // already reaches the slot card via the controllers() snapshot + stampSlotPath;
        // this is the transient "what just happened" notice (claim failed / rolled
        // back / needs replug), routed to the one-shot errorMessage toast channel.
        void notice(const reducer::UsbController& c, reducer::UsbNotice n) override {
            owner_->onUsbNotice(c, n);
        }

      private:
        AppModel* owner_;
    };

    source::UsbPathPreferenceRepository usbPathRepo_;
    source::UsbPathPreferenceStore usbPathStore_;
    std::unique_ptr<source::usb::WinHidGateway> usbGateway_;
    UsbObserver usbObserver_;
    std::unique_ptr<source::usb::UsbGamepadManager> usbManager_;
    QTimer* usbScanTimer_;
    reducer::PollRateSampler usbPollSampler_;
    // Latest independently-measured USB-direct poll rate (URB completion rate)
    // per controllers() map key — the same int the synthetic slot id is built
    // from. Written on the main thread by pollUsbDirect (from usbPollSampler_),
    // read in rebuild() to stamp ControllerSlot::liveRates.directPollHz. A
    // synthetic that leaves Direct is pruned so a reused key can't show a stale
    // rate.
    QHash<int, int> usbPollRateHz_;
    // The VID:PIDs of SDL devices seen on the last syncFrameworkPresence pass, so
    // the next pass can emit FrameworkUp/Down deltas to the FSM. Main-thread-only.
    QSet<int> lastFrameworkVpKeys_;

    // ── Live input-rate measurement (android parity) ─────────────────────────
    // The per-slot live-rate StateSource: it samples the hot-path event counters
    // the GamepadInputProcessor exposes (inputCounters) through a pure
    // InputRateTracker to derive quantized gamepad/motion Hz + peaks. Built in the
    // ctor body (its CounterSource borrows processor_). Driven by inputRateTimer_
    // at ~1 Hz on the main thread (the android InputRateStore samples on a similar
    // sub-second loop). inputRatesSub_ folds each emission into state_.slotList so
    // the slot card repaints; the cache survives slot-list rebuilds so a rebuild
    // triggered by an unrelated change keeps the last measured numbers.
    std::unique_ptr<source::InputRateStore> inputRateStore_;
    arch::Observable<source::SlotInputRatesMap>::Subscription inputRatesSub_;
    QTimer* inputRateTimer_;
    // slotId -> last measured gamepad/motion Hz (+peaks), the InputRateStore's
    // latest emission projected to the model's value type. directPollHz is filled
    // separately from usbPollRateHz_ at rebuild(); this carries the SDL/HID stream
    // rates. Read on the main thread only.
    QHash<QString, models::SlotLiveRates> liveRatesBySlot_;

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
