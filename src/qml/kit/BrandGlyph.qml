// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Thin wrapper over the v6 brand SVGs embedded in the binary (packaging/dish.qrc
// → `:/brand/*.svg`, addressed from QML as `qrc:/brand/<name>.svg`). The Qt SVG
// image plugin (Qt6::Svg, already linked) renders them. Pass a bare glyph name
// (no path, no extension); for the contract's connection glyph tokens
// ("satelliteBase"/"satelliteConnected"/"satelliteOff") use `glyphForToken()`.
//
// The root id must NOT be named `glyph`: in QML an id shadows a same-named
// property in its own component scope, so `"qrc:/brand/" + glyph` would
// concatenate the Image OBJECT (a garbage URL) and every glyph in the app
// silently rendered nothing — exactly what happened until this rename.

import QtQuick

Image {
    id: root

    // Bare brand asset name, e.g. "satellite-connected" (no dir, no ".svg").
    property string glyph: "satellite"

    // An empty glyph (a hidden slot, e.g. SectionHeader/rail items without an
    // icon) must not attempt a load — "qrc:/brand/.svg" warns on every create.
    source: root.glyph.length > 0 ? "qrc:/brand/" + root.glyph + ".svg" : ""
    // SVGs render crisp at any raster size if we request the exact target px.
    sourceSize.width: width
    sourceSize.height: height
    fillMode: Image.PreserveAspectFit
    smooth: true

    // Resolve a ConnectionListModel `glyph` role token to a brand asset name.
    function glyphForToken(token) {
        if (token === "satelliteConnected")
            return "satellite-connected";
        if (token === "satelliteOff")
            return "satellite-off";
        return "satellite";            // "satelliteBase" + fallback
    }
}
