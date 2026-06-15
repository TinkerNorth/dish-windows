// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// WakeStateController — the EFFECT half of the wake subsystem: a kernel
// Controller that subscribes the WakeStateComposer's Observable<WakeState> and
// drives the OS display-sleep inhibitor (Windows: SetThreadExecutionState, via
// DisplaySleepInhibitor) on each value. Mirrors dish-android composer/
// WakeStateController (AbstractController<WakeState>).
//
// SoC: the DERIVE (count -> WakeState) lives in WakeStateComposer; this class
// only actuates. It holds no domain state beyond the inhibitor handle and a
// `stopped` guard. Contract preserved from android + the old ScreenWakeController:
//   * start() is idempotent (kernel guard) and applies the current value
//     immediately, so a controller started while already streaming acquires at
//     once.
//   * onStarting() re-arms the stopped guard so a stop()ped controller can be
//     restarted.
//   * stop() is a DELIBERATE teardown: it cancels the subscription AND releases
//     the inhibitor (don't strand the wake flag when streaming ends / the app
//     backgrounds). This overrides the kernel default (which only cancels).
//   * The 0<->positive no-thrash contract is preserved end-to-end: the composer
//     emits distinct WakeStates only, and the inhibitor's acquire/release are
//     idempotent, so a noisy upstream never thrashes SetThreadExecutionState.
//
// The android wifi-lock arm is dropped (phone-only).

#pragma once

#include "architecture/Controller.h"
#include "composer/WakeStateComposer.h"

#include <QString>

namespace dish::util {
class DisplaySleepInhibitor;
}

namespace dish::composer {

class WakeStateController : public arch::Controller<WakeState> {
  public:
    // `inhibitor` is borrowed (owned by the composition root); may be nullptr in
    // a headless/stripped build, in which case the controller bookkeeps without
    // effecting anything. `reason` is the human string handed to the OS portal.
    WakeStateController(
        const arch::Observable<WakeState>& wakeState, util::DisplaySleepInhibitor* inhibitor,
        QString reason = QStringLiteral("Dish is streaming gamepad input to Satellite"));

    // True iff the inhibitor is currently held (test/diagnostic accessor).
    bool isInhibiting() const;

  protected:
    void apply(const WakeState& value) override;
    void onStarting() override;

  public:
    // Deliberate teardown: cancel the subscription AND release the inhibitor.
    void stop() override;

  private:
    util::DisplaySleepInhibitor* inhibitor_;
    QString reason_;
    bool stopped_ = false;
};

} // namespace dish::composer
