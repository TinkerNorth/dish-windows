// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Borderless monospace telemetry readout ("120 Hz", "~3.4 ms") — the quiet
// counterpart to the filled capability pills so live numbers read as telemetry,
// not status.
//
// This component OWNS the rate/latency formatting. Nothing else in the app
// formats a rate:
//   `~` means "derived or estimated", once. It is permanent on latency (always
//   median round-trip ÷ 2) and conditional on Hz (only when the rate is not
//   continuously measured).
//   COLOUR carries live-vs-idle ONLY — a measured rate on a dead wire is muted,
//   an estimated rate on a live wire is not.
//   A latency with no samples renders as nothing. Never "~0 ms".

import QtQuick
import Dish.Chrome

Text {
    // Is the thing being reported live right now (colour), NOT whether the
    // number was measured (that is the `~`).
    property bool live: false

    font.family: Tokens.monoFamily
    font.pixelSize: Tokens.textChip
    color: live ? Theme.success : Theme.muted

    // "1000 Hz" when measured, "~1000 Hz" when estimated, "" when there is no
    // rate to report.
    function rateText(hz, measured) {
        if (!hz || hz <= 0)
            return "";
        return (measured ? "" : "~") + Math.round(hz) + " Hz";
    }

    // "~3.4 ms" — always estimated. Empty until at least one sample exists, and
    // "<1 ms" below the millisecond: the figure is real but under this
    // instrument's resolution, and "~0.0 ms" would claim a latency no link has.
    // Mirrors reducer::formatLatencyMs, which is what the models actually
    // render — the two must never disagree (plan D47).
    function latencyText(ms, samples) {
        if (!samples || samples <= 0)
            return "";
        const tenths = Math.round(Math.max(0, Number(ms)) * 10);
        if (tenths < 10)
            return "<1 ms";
        return "~" + (tenths / 10).toFixed(1) + " ms";
    }
}
