// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The uppercase, tracked-out monospace section label in the ACCENT color — the
// structural spine of every Dish screen (CONTROLLERS, FOUND, REMEMBERED).
// Optionally pairs with a leading brand glyph, as the Connections sections do.
// Kept a Row so the glyph slot costs nothing when unused.
//
// Callers keep passing natural-cased `label`; the casing is applied here as a
// FONT property, not by uppercasing the string — a .toUpperCase() in QML uses
// the C locale and would mangle e.g. Turkish dotless i.

import QtQuick
import Dish.Chrome

Row {
    id: header

    property string label: ""
    // Optional brand asset name ("satellite", "dish", ...) drawn before the
    // text. Empty = text only.
    property string glyph: ""

    spacing: Tokens.s3

    BrandGlyph {
        glyph: header.glyph
        width: Tokens.glyphSm
        height: Tokens.glyphSm
        visible: header.glyph.length > 0
        anchors.verticalCenter: parent.verticalCenter
    }

    Text {
        text: header.label
        color: Theme.primary
        font.family: Tokens.monoFamily
        font.pixelSize: Tokens.textMeta
        font.weight: Font.DemiBold
        font.letterSpacing: Tokens.sectionLetterSpacing
        font.capitalization: Font.AllUppercase
        anchors.verticalCenter: parent.verticalCenter
    }
}
