// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The app's ONE tooltip. A bare `ToolTip` resolves to the Basic style's
// delegate, which paints `palette.toolTipBase` on `palette.toolTipText` — Qt's
// system defaults, not ours. On the light appearance that is a white slab on a
// white card with no edge (the tip is legible only where it happens to overlap
// something darker); on the dark one it is an unthemed white block in the
// middle of a night-blue window. Either way it was the one surface in the app
// that never read the palette.
//
// This carries the vocabulary every other floating surface already uses (the
// Home row menu, the dialogs): the elevated `surface` fill, a hairline
// `outline` edge so it separates from a same-coloured card underneath,
// `radiusButton`, and body text at `onSurface`. Both appearances resolve from
// tokens, so neither can drift.
//
// It is a plain ToolTip subtype, so every call site keeps the DECLARED (never
// attached) form the rail, the title bar and the Home nodes use — see the note
// at those sites for why the attached property cannot work here.
//
// Sizing is deliberately Basic's: one line, sized to its text. A tip is a
// label, not a paragraph; the one long sentence in the app (the wire's
// latency explainer) still fits inside the 900 px minimum window, and a Popup
// that would overhang the window edge is shifted back in by `margins`.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

ToolTip {
    id: control

    // The Windows dwell. Call sites may shorten it (an elided name wants a
    // faster reveal than a navigation label), never lengthen it.
    delay: 500

    topPadding: Tokens.s3
    bottomPadding: Tokens.s3
    leftPadding: Tokens.s4
    rightPadding: Tokens.s4

    background: Rectangle {
        color: Theme.surface
        border.width: 1
        border.color: Theme.outline
        radius: Tokens.radiusButton
    }

    contentItem: Text {
        text: control.text
        color: Theme.onSurface
        font.pixelSize: Tokens.textSummary
        wrapMode: Text.WordWrap
    }
}
