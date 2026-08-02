// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Tracked-out uppercase mono micro-label in the accent color — the "eyebrow"
// above dialog headings and hero titles (design FEyebrow). For the structural
// section label with the optional leading glyph use SectionHeader.
//
// Callers pass NATURAL case; the casing is a font property, never a string
// transform — an uppercased literal cannot be translated.

import QtQuick
import Dish.Chrome

Text {
    property bool mutedTone: false

    font.family: Tokens.monoFamily
    font.pixelSize: Tokens.textChip
    font.weight: Font.DemiBold
    font.letterSpacing: Tokens.sectionLetterSpacing
    font.capitalization: Font.AllUppercase
    color: mutedTone ? Theme.muted : Theme.primary
}
