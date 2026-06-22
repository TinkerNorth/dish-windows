// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// WakeStateComposer — a kernel Composer that PURELY derives the wake intent:
// should Dish keep the display awake right now? It folds two upstreams — the
// count of slots currently streaming gamepad input to a satellite, and a
// "keep screen on" preference/override count — into a WakeState. Mirrors
// dish-android composer/WakeStateComposer (AbstractComposer<WakeState>):
// streamingSlotCount + shouldKeepScreenOn > 0.
//
// SoC: this is the DERIVE half of the wake subsystem. It touches no OS power
// API — that is WakeStateController's job (the EFFECT half). The transform is a
// pure free function over the two snapshots so it unit-tests in isolation, and
// WakeState is ==-comparable so the Combiner's distinct-until-changed gives the
// 0<->positive no-thrash contract for free (the controller never sees a same-
// value re-emit). Qt-free.
//
// The android wifi-lock arm is dropped (phone-only); the Windows effect is a
// single SetThreadExecutionState assertion, so one boolean intent is enough.

#pragma once

#include "architecture/Composer.h"
#include "architecture/Observable.h"

namespace dish::composer {

// The derived wake intent. `shouldInhibit` is the actuator signal the controller
// reads; `streamingSlotCount` is carried for diagnostics and to keep the value
// distinct across count changes that don't flip the bool (so a probe can see the
// derivation react) — but the controller only acts on shouldInhibit.
struct WakeState {
    bool shouldInhibit = false;
    int streamingSlotCount = 0;

    bool operator==(const WakeState& o) const {
        return shouldInhibit == o.shouldInhibit && streamingSlotCount == o.streamingSlotCount;
    }
    bool operator!=(const WakeState& o) const { return !(*this == o); }
};

// Pure derivation: keep the display awake iff at least one slot is streaming OR
// a keep-screen-on override is in force. Mirrors android's
// `streamingSlotCount > 0 || shouldKeepScreenOn > 0`. Pure.
inline WakeState deriveWakeState(int streamingSlotCount, int shouldKeepScreenOn) {
    const bool inhibit = streamingSlotCount > 0 || shouldKeepScreenOn > 0;
    return WakeState{inhibit, streamingSlotCount};
}

// The Composer: combines the streaming-slot-count and keep-screen-on Observables
// through deriveWakeState.
class WakeStateComposer : public arch::Composer<WakeState, int, int> {
  public:
    WakeStateComposer(const arch::Observable<int>& streamingSlotCount,
                      const arch::Observable<int>& shouldKeepScreenOn)
        : arch::Composer<WakeState, int, int>(streamingSlotCount, shouldKeepScreenOn,
                                              deriveWakeState) {}
};

} // namespace dish::composer
