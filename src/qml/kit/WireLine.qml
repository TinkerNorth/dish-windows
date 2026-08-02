// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The pad -> host wire: a 2px line with the dish glyph riding its middle and an
// optional mono caption centred above it. Promoted out of HomePage.qml's
// private `component WireLine` — Home's wiring rows, the wizard banner and
// Configure binding all draw it, and a variant that lives in a page is how a
// design system dies.
//
// `live` is the only state that claims a working link: solid accent plus
// `dish-connected`. `transmitting` is the apply-in-flight state — the dashes
// stay dashes and crawl toward the host, because the wire must NOT read live
// until the bind has actually succeeded. The crawling glyph is the BASE `dish`
// asset, never a `*-animated` file: those ship no <animate> elements and Qt
// runs no SMIL, so they are static pictures of a state.
//
// The dash is a Canvas (Qt 6.7 has no dashed-line primitive) and takes its
// colour through Qt.rgba(c.r, c.g, c.b, c.a). NEVER String(Theme.outline): a
// QColor stringifies to #AARRGGBB and Canvas 2D parses that as #RRGGBBAA, so a
// translucent token silently paints the wrong colour at the wrong alpha.

import QtQuick
import Dish.Chrome

Item {
    id: wire

    // A working link: solid accent line, `dish-connected` at the centre.
    property bool live: false
    // Mono caption centred above the line ("as Xbox 360 · rumble", "~250 Hz").
    property string label: ""
    // The mid-wire dish glyph; false leaves a bare rule (a ghost row).
    property bool showGlyph: true
    // An apply is in flight: accent dashes crawling toward the host.
    property bool transmitting: false

    // Distance (px) into the 8px dash pattern. Animated while transmitting;
    // read by the Canvas as a NEGATIVE lineDashOffset so the pattern walks
    // left-to-right, i.e. toward the host. Declared WITHOUT an initialiser: an
    // initial value is a binding, and a binding plus the NumberAnimation value
    // source below is a qmllint error (duplicate-property-binding). A real
    // defaults to 0 anyway, and onTransmittingChanged parks it there.
    property real dashPhase

    // Idle is the outline hairline; an apply lifts the dashes to the accent so
    // "in flight" is visible without claiming the solid, live wire.
    readonly property color lineColor: (wire.live || wire.transmitting) ? Theme.primary
                                                                        : Theme.outline

    // The wizard banner hands the wire `fillWidth` with this as its floor.
    implicitWidth: 60
    implicitHeight: stack.implicitHeight

    Column {
        id: stack
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: Tokens.s2

        Text {
            id: caption
            visible: wire.label.length > 0
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: wire.label
            elide: Text.ElideRight
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textChip
            color: (wire.live || wire.transmitting) ? Theme.primary : Theme.mutedStrong
        }

        Item {
            id: line
            width: parent.width
            height: wire.showGlyph ? Tokens.glyphMd : 2

            Canvas {
                id: dashCanvas
                anchors.fill: parent

                onPaint: {
                    const ctx = getContext("2d");
                    ctx.reset();
                    const c = wire.lineColor;
                    ctx.strokeStyle = Qt.rgba(c.r, c.g, c.b, c.a);
                    ctx.lineWidth = 2;
                    if (!wire.live)
                        ctx.setLineDash([4, 4]);
                    ctx.lineDashOffset = -wire.dashPhase;
                    const y = height / 2;
                    // The glyph sits in a gap of its own radius plus one step of
                    // the spacing scale on each side (the design's 8px wire gap).
                    const gap = wire.showGlyph ? Tokens.glyphMd / 2 + Tokens.s4 : 0;
                    ctx.beginPath();
                    ctx.moveTo(0, y);
                    ctx.lineTo(Math.max(0, width / 2 - gap), y);
                    ctx.moveTo(Math.min(width, width / 2 + gap), y);
                    ctx.lineTo(width, y);
                    ctx.stroke();
                }

                Connections {
                    target: Theme
                    function onPaletteChanged() { dashCanvas.requestPaint(); }
                }
                Connections {
                    target: wire
                    function onLiveChanged() { dashCanvas.requestPaint(); }
                    function onTransmittingChanged() { dashCanvas.requestPaint(); }
                    function onShowGlyphChanged() { dashCanvas.requestPaint(); }
                    function onDashPhaseChanged() { dashCanvas.requestPaint(); }
                }
            }

            BrandGlyph {
                id: dish
                visible: wire.showGlyph
                anchors.centerIn: parent
                width: Tokens.glyphMd
                height: Tokens.glyphMd
                glyph: wire.live ? "dish-connected" : wire.transmitting ? "dish" : "dish-off"

                // The one sanctioned transient for a brand glyph: an opacity
                // pulse over the base asset, stopped dead by the OS preference.
                SequentialAnimation on opacity {
                    running: wire.transmitting && !wire.live && wire.visible
                             && !Tokens.reducedMotion
                    loops: Animation.Infinite
                    alwaysRunToEnd: true
                    NumberAnimation { to: 0.45; duration: Tokens.durBusy / 2 }
                    NumberAnimation { to: 1.0; duration: Tokens.durBusy / 2 }
                }
            }
        }
    }

    // One full pattern period (4 on + 4 off) per busy tick.
    NumberAnimation on dashPhase {
        running: wire.transmitting && !wire.live && wire.visible && !Tokens.reducedMotion
        loops: Animation.Infinite
        from: 0
        to: 8
        duration: Tokens.durBusy
    }

    onTransmittingChanged: {
        // Park the pattern on a phase boundary so a stopped wire never freezes
        // mid-crawl at an arbitrary offset.
        if (!wire.transmitting)
            wire.dashPhase = 0;
    }
}
