// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The uppercase, tracked-out monospace section label in the ACCENT color — the
// structural spine of every Dish screen (CONTROLLERS, FOUND, REMEMBERED).
// Optionally pairs with a leading brand glyph, as the Connections sections do.
// Kept a Row so the glyph slot costs nothing when unused; callers keep passing
// natural-cased `label` and the casing is applied here.

import QtQuick
import Dish.Chrome

Row {
    id: header

    property string label: ""
    // Optional brand asset name ("satellite", "dish", ...) drawn before the
    // text at 16px. Empty = text only.
    property string glyph: ""

    spacing: Tokens.s3

    BrandGlyph {
        glyph: header.glyph
        width: 16
        height: 16
        visible: header.glyph.length > 0
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        text: header.label.toUpperCase()
        color: Theme.primary
        font.family: Tokens.monoFamily
        font.pixelSize: Tokens.textMeta
        font.letterSpacing: Tokens.sectionLetterSpacing
        anchors.verticalCenter: parent.verticalCenter
    }
}
