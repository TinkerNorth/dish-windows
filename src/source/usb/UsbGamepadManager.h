// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// UsbGamepadManager — the Windows USB-direct claim driver. Port of dish-android
// source/usb/UsbGamepadManager.kt (the imperative coordinator around the pure
// UsbPathMachine FSM).
//
// World signals (USB attach/detach, framework presence, the user's path pick,
// claim results, transition timeouts) become UsbEvents; `reduce` (the pure FSM
// in core/reducer/UsbPathMachine) decides the next state and the effect list;
// this class executes those effects against the real subsystems — the raw-HID
// claim Gateway (UsbDeviceGateway), the input pipeline (GamepadInputProcessor),
// and the UI/registry observer. It is the ONLY USB-IO place, mirroring android.
//
// ── The Windows platform reality (why this benefits fewer pads than android) ──
// On android, USB-direct claims the device to read raw HID reports, bypassing
// the framework input-rate cap and framework deadzones. On Windows, SDL/XInput
// already reads pads at full rate, so USB-direct is an ALTERNATIVE input source
// whose benefit is narrower. Crucially: **XInput hides Xbox-class pads from raw
// HID** — an Xbox 360/One/Series pad bound to XInput simply does not appear to a
// raw-HID/WinUSB enumerator. So this path mainly serves HID pads (DualSense /
// DualShock 4 / 8BitDo); Xbox pads stay on SDL/XInput. The Gateway's enumerate()
// excludes XInput-claimed pads, and a model that fails to claim (Busy/etc.)
// falls back to the SDL/XInput "framework" path via the FSM's Routed phase —
// USB-direct is strictly additive and opt-in/auto-selected, never a regression
// to the SDL path.
//
// ── Hot-path discipline (this is the one Wave-2 slice on the input hot path) ──
// The Gateway's read loop is plain C++ on its own thread and feeds decoded
// reports straight into GamepadInputProcessor::publish — the SAME publish path
// the SDL bridge uses — with NO per-report allocation (the HID->XUSB button map
// is the packed-int GamepadButtonLayouts math) and NO Qt signals on the per-
// report path. The FSM decision is a pure function run off the report path; all
// FSM mutation is serialized through applyEvent so events apply in order.

#pragma once

#include "source/usb/UsbDeviceGateway.h"

#include "core/reducer/UsbPathMachine.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace dish::input {
class GamepadInputProcessor;
}

namespace dish::source {
class UsbPathPreferenceStore;
}

namespace dish::source::usb {

// The non-input side-effects the FSM produces, surfaced for the UI/registry to
// render. The C++ analogue of the android registry+notification calls the
// effector makes (markDirectFailed, beginModelTransition, markNeedsReplug,
// notify(...), ...). Default no-op so headless/test wiring can ignore them.
class UsbDirectObserver {
  public:
    virtual ~UsbDirectObserver() = default;

    // A user-facing banner reason fired (the UI maps notice->localized string).
    virtual void notice(const reducer::UsbController& /*c*/, reducer::UsbNotice /*notice*/) {}
    // The controller card's persistent state marks.
    virtual void markFailure(int /*vendorId*/, int /*productId*/,
                             reducer::DirectClaimFailure /*reason*/) {}
    virtual void clearFailure(int /*vendorId*/, int /*productId*/) {}
    virtual void markNeedsReplug(int /*vendorId*/, int /*productId*/) {}
    virtual void markRestoreStuck(int /*vendorId*/, int /*productId*/) {}
    virtual void clearRestoreStuck(int /*vendorId*/, int /*productId*/) {}
    virtual void beginHold(int /*vendorId*/, int /*productId*/) {}
    virtual void endHold(int /*vendorId*/, int /*productId*/) {}
    // Bind the carried connection to a device id (framework or synthetic).
    virtual void bind(int /*deviceId*/, const std::string& /*connId*/, int /*type*/) {}
    // A synthetic device was registered / removed (the UI shows it as a slot).
    virtual void syntheticAdded(int /*syntheticId*/, const std::string& /*name*/, bool /*hasGyro*/,
                                int /*pollRateHz*/, int /*vendorId*/, int /*productId*/) {}
    virtual void syntheticRemoved(int /*syntheticId*/) {}
};

class UsbGamepadManager {
  public:
    // `gateway` is the IO seam (real WinHidGateway in production, a fake in
    // tests). `processor` receives decoded reports on the gateway read thread
    // (may be null in tests that only assert FSM/claim outcomes). `prefs` is the
    // per-VID:PID path store (may be null; then Auto resolution always applies).
    // `observer` receives the non-input effects (may be null -> a no-op).
    // `nextSyntheticId` lets a test pin the synthetic id the gateway-less path
    // would assign; production uses the gateway's returned id.
    UsbGamepadManager(UsbDeviceGateway* gateway, input::GamepadInputProcessor* processor,
                      UsbPathPreferenceStore* prefs, UsbDirectObserver* observer);

    // Re-scan the USB bus and (idempotently) start tracking present pads, driving
    // each toward its resolved path automatically (not user-initiated). Safe to
    // call repeatedly (e.g. on app foreground). Mirrors android reconcileForeground.
    void reconcile();

    // The user explicitly picks Direct for a model (the Settings toggle / card
    // action). Persists the pick and drives a user-initiated Choose(Direct).
    void tryDirectMode(int vendorId, int productId);

    // The user explicitly picks `choice` for a model. Persists + drives a
    // user-initiated Choose. Mirrors android setPathChoice.
    void setPathChoice(int vendorId, int productId, reducer::PathChoice choice);

    // ── World-signal entry points (the driver turns these into events) ────────
    // Framework (SDL/XInput) device for this model appeared / disappeared.
    void onFrameworkUp(int vendorId, int productId, int frameworkId);
    void onFrameworkDown(int vendorId, int productId);
    // The device was physically unplugged.
    void onUsbGone(int vendorId, int productId);
    // The transition timer for a model elapsed (the driver normally owns the
    // timer; exposed so a test can fire it deterministically).
    void fireTimeout(int vendorId, int productId);

    // The live FSM state per model (vpKey -> controller). Read-only snapshot.
    std::map<int, reducer::UsbController> controllers() const;
    std::optional<reducer::UsbController> controllerFor(int vendorId, int productId) const;

  private:
    static int vpKey(int vendorId, int productId) {
        return (vendorId << 16) | (productId & 0xFFFF);
    }

    // Resolve a model's desired path from the stored pick + fast-lane + prior
    // failure (the pure resolvePathChoice policy).
    reducer::PathChoice resolvePath(int vendorId, int productId) const;

    // Ensure a controller entry exists for a present device, then drive it toward
    // its resolved path (not user-initiated). Idempotent.
    void ensureTracked(const UsbDeviceInfo& device);

    // Run the reducer for one event and execute its effects. All FSM mutation
    // funnels through here so events apply in order.
    void applyEvent(int key, const reducer::UsbEvent& event);
    void execute(int key, const reducer::UsbController& c, const reducer::UsbEffect& fx);

    // Effectors.
    void runClaim(int key);
    void runReclaim(int key, const reducer::UsbController& c);
    // Open + claim + read-loop bring-up; classify the outcome. The single place
    // an open/claim outcome becomes a DirectClaimFailure (the UsbGamepadManager
    // tests pin this classification).
    ClaimResult doClaim(const UsbDeviceInfo& device);

    UsbDeviceGateway* gateway_;
    input::GamepadInputProcessor* processor_;
    UsbPathPreferenceStore* prefs_;
    UsbDirectObserver* observer_;

    mutable std::mutex mtx_; // guards the maps below.
    std::map<int, reducer::UsbController> controllers_;
    // The last-known device descriptor per tracked model (for re-claim).
    std::unordered_map<int, UsbDeviceInfo> devices_;
    // In-memory prior-failure tracking (android keeps this on the registry; we
    // fold it in so the manager is self-contained). Cleared on a fresh plug-in.
    std::unordered_map<int, reducer::DirectClaimFailure> priorFailures_;
};

} // namespace dish::source::usb
