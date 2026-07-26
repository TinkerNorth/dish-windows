// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Borderless monospace telemetry readout ("120 Hz", "Motion 250 Hz") — the
// quiet counterpart to the filled capability pills so live numbers read as
// telemetry, not status. `live` renders the success token (a continuously
// measured rate); otherwise muted (an estimated/peak rate).

import QtQuick
import Dish.Chrome

Text {
    property bool live: false

    font.family: Tokens.monoFamily
    font.pixelSize: Tokens.textChip
    color: live ? Theme.success : Theme.muted
}
