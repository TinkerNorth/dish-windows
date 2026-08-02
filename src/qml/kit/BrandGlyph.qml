// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Thin wrapper over the v6 brand SVGs embedded in the binary (packaging/dish.qrc
// → `:/brand/*.svg`, addressed from QML as `qrc:/brand/<name>.svg`). The Qt SVG
// image plugin (Qt6::Svg, already linked) renders them. Pass a bare glyph name
// (no path, no extension); for the contract's connection glyph tokens
// ("satelliteBase"/"satelliteConnected"/"satelliteOff") use `glyphForToken()`.
//
// COLOUR: a glyph re-tints by PALETTE, never by state — state colour lives in
// the dot and the chip. The shipped SVGs are hard-coded #8FCFE3, which is right
// on the dark surface and near-invisible on the light one, so the light palette
// routes the image through a MultiEffect colorization to Theme.glyph. The dark
// palette renders the raw Image: full fidelity (the white inset highlights
// survive), zero effect cost, which is the common case.
//
// MOTION: the six `*-animated.svg` files contain no <animate> elements and Qt
// runs no SMIL — they are static files that look like states. They are
// unreachable here on purpose: an "-animated" name resolves to its family base
// and the transient is expressed in QML (DishProgressBar, or an opacity /
// rotation animation over the base glyph, gated on Tokens.reducedMotion).
//
// The root id must NOT be named `glyph`: in QML an id shadows a same-named
// property in its own component scope, so `"qrc:/brand/" + glyph` would
// concatenate the Image OBJECT (a garbage URL) and every glyph in the app
// silently rendered nothing — exactly what happened until this rename.

import QtQuick
import QtQuick.Effects
import Dish.Chrome

Image {
    id: root

    // Bare brand asset name, e.g. "satellite-connected" (no dir, no ".svg").
    property string glyph: "satellite"
    // false = decorative / raw: render the shipped cyan untouched.
    property bool tinted: true
    // Non-empty makes the glyph meaningful: it stops being ignored by
    // accessibility and announces this name. Decorative glyphs beside a text
    // label leave it empty.
    property string accessibleName: ""

    // An "-animated" variant falls back to its family base (see MOTION above).
    readonly property string asset: root.glyph.indexOf("-animated") >= 0
                                    ? root.glyph.substring(0, root.glyph.indexOf("-"))
                                    : root.glyph

    // The dark palette ships the glyphs' own cyan; only a palette whose glyph
    // tint differs from it needs the colorization pass. Qt.colorEqual, not
    // `!==`: a QColor is never strictly equal to a string.
    readonly property bool recolour: root.tinted && !Qt.colorEqual(Theme.glyph, "#8fcfe3")

    // An empty glyph (a hidden slot, e.g. SectionHeader/rail items without an
    // icon) must not attempt a load — "qrc:/brand/.svg" warns on every create.
    source: root.asset.length > 0 ? "qrc:/brand/" + root.asset + ".svg" : ""
    // SVGs render crisp at any raster size if we request the exact target px.
    sourceSize.width: width
    sourceSize.height: height
    fillMode: Image.PreserveAspectFit
    smooth: true

    Accessible.role: Accessible.Graphic
    Accessible.name: root.accessibleName
    Accessible.ignored: root.accessibleName.length === 0

    layer.enabled: root.recolour
    layer.effect: MultiEffect {
        colorization: 1.0
        colorizationColor: Theme.glyph
    }

    // Resolve a ConnectionListModel `glyph` role token to a brand asset name.
    function glyphForToken(token) {
        if (token === "satelliteConnected")
            return "satellite-connected";
        if (token === "satelliteOff")
            return "satellite-off";
        return "satellite";            // "satelliteBase" + fallback
    }
}
