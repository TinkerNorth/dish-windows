// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.

#include "composer/WakeStateController.h"

#include "Util/DisplaySleepInhibitor.h"

namespace dish::composer {

WakeStateController::WakeStateController(const arch::Observable<WakeState>& wakeState,
                                         util::DisplaySleepInhibitor* inhibitor, QString reason)
    : arch::Controller<WakeState>(wakeState), inhibitor_(inhibitor), reason_(std::move(reason)) {}

bool WakeStateController::isInhibiting() const {
    return inhibitor_ != nullptr && inhibitor_->isHeld();
}

void WakeStateController::apply(const WakeState& value) {
    // Drop any emission that lands after a deliberate stop() (parity with
    // android's `if (stopped) return` guard; the subscription is already torn
    // down by the kernel, so this is belt-and-suspenders).
    if (stopped_) { return; }
    if (inhibitor_ == nullptr) { return; }
    // acquire/release are idempotent, so a same-intent re-emit is a no-op and the
    // OS portal is only touched on a real 0<->positive flip.
    if (value.shouldInhibit) {
        inhibitor_->acquire(reason_);
    } else {
        inhibitor_->release();
    }
}

void WakeStateController::onStarting() {
    // Re-arm so a restarted controller actuates again.
    stopped_ = false;
}

void WakeStateController::stop() {
    stopped_ = true;
    cancelCollection();
    // Deliberate teardown: never strand the wake flag when streaming ends.
    if (inhibitor_ != nullptr) { inhibitor_->release(); }
}

} // namespace dish::composer
