// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The monospace, letter-spaced section label used across the app (mirrors the
// Widgets `sectionHeaderQss()` treatment: uppercase, muted, wide tracking). Use
// it as the heading of any page or card group.

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome

Label {
    // Section headers are conventionally uppercased + tracked; we keep the API
    // a plain `text` and apply the casing here so callers pass natural strings.
    property string label: ""
    text: label.toUpperCase()

    color: Theme.muted
    font.family: "Consolas"
    font.pixelSize: 11
    font.bold: true
    font.letterSpacing: 2
}
