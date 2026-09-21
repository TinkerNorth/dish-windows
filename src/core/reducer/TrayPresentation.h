// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// What the tray item shows. There is deliberately no "hidden" or "passive"
// state: a hidden item is a running Dish the user can neither reach nor quit,
// so the item stays in the notification area for as long as it is registered
// and only its icon and tooltip follow the streaming state.

#pragma once

#include <cstdint>

namespace dish::reducer {

enum class TrayActivity : std::uint8_t { Idle, Streaming };

struct TrayPresentation {
    TrayActivity activity = TrayActivity::Idle;
    int streamingSlots = 0;
    bool windowVisible = true;

    bool operator==(const TrayPresentation& o) const {
        return activity == o.activity && streamingSlots == o.streamingSlots &&
               windowVisible == o.windowVisible;
    }
    bool operator!=(const TrayPresentation& o) const { return !(*this == o); }
};

inline TrayPresentation deriveTrayPresentation(bool windowVisible, int streamingSlots) {
    const int active = streamingSlots > 0 ? streamingSlots : 0;
    return TrayPresentation{active > 0 ? TrayActivity::Streaming : TrayActivity::Idle, active,
                            windowVisible};
}

} // namespace dish::reducer
