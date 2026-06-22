// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// streamingSlotCount — the pure derivation of "how many slots are actively
// streaming gamepad input to a satellite right now" from the connection layer's
// binding table (slotId -> connectionId) crossed with the per-connection live
// link state. This is the upstream count the WakeStateComposer folds into a
// WakeState. Extracted from the old util::ScreenWakeController::streamingCount so
// the arithmetic stays unit-testable without standing up a controller.
//
// Lives in composer/ (not core/) because it speaks the Qt + models vocabulary of
// 2b's connection layer (QHash, models::LinkState); WakeStateComposer.h itself
// stays Qt-free (it combines plain int Observables, mirroring android).

#pragma once

#include "Models/Models.h"

#include <QHash>
#include <QString>

namespace dish::composer {

// Count bindings whose connection is LinkState::Connected (the only state that
// is actually exchanging packets). A Saved/Connecting/Ready/Found/Stale/Unstable
// binding is paired or pending, not streaming, so it doesn't keep the display
// awake. An unknown connection id counts as not-streaming.
inline int streamingSlotCount(const QHash<QString, QString>& bindings,
                              const QHash<QString, models::LinkState>& connectionStates) {
    int count = 0;
    for (auto it = bindings.begin(); it != bindings.end(); ++it) {
        if (connectionStates.value(it.value(), models::LinkState::Saved) ==
            models::LinkState::Connected) {
            ++count;
        }
    }
    return count;
}

} // namespace dish::composer
