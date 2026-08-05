// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer's signature: a fixed 236px rail drawing the install AS a
// transmission — satellite above, this PC's dish below, a vertical beam
// between them. Idle: dashed dim beam. Installing: the beam goes accent,
// dashes crawl toward the dish, packets travel, the glyphs pulse in offset
// phase. Done: solid beam, both ends -connected. Uninstall runs the story
// backwards (crawl upward, no packets). Error: back to idle — red never
// enters the scene, failures live in the content column.
//
// Every loop gates on visible && !Tokens.reducedMotion and stops the moment
// its meaning ends; the scene is static except while work actually happens.
// The scene composes STATIC brand SVG poses (BrandGlyph maps *-animated names
// to their base); ALL motion is QML-native.

// Bound: the starfield and packet Repeater delegates read outer ids.
pragma ComponentBehavior: Bound

import QtQuick
import Dish.Chrome
import Dish.Chrome as Kit

Item {
    id: rail

    // "idle" | "installing" | "done" | "removing" | "removed"
    property string sceneState: "idle"
    property string versionText: ""
    // Empty values render as the em-dash placeholder in mutedStrong.
    property string destinationText: ""
    property string onDiskText: ""
    // Extreme-text-scale fallback: drop the starfield and orbit, keep the
    // beam and glyphs; the manifest never drops (facts move, never vanish).
    property bool compact: false

    implicitWidth: Tokens.railExpanded

    readonly property bool installing: rail.sceneState === "installing"
    readonly property bool removing: rail.sceneState === "removing"
    readonly property bool settled: rail.sceneState === "done"

    readonly property string satGlyph: rail.installing ? "satellite-broadcasting"
                                     : rail.settled ? "satellite-connected"
                                     : rail.sceneState === "removed" ? "satellite-off"
                                     : "satellite"
    readonly property string dishGlyph: rail.installing ? "dish-receiving"
                                      : rail.settled ? "dish-connected"
                                      : rail.removing ? "dish"
                                      : "dish-off"

    // Park both glyphs at full opacity the instant the pulse loses its
    // meaning, so a stopped scene never freezes mid-pulse.
    onSceneStateChanged: {
        if (!rail.installing && !rail.removing) {
            satGlyphItem.opacity = 1.0;
            dishGlyphItem.opacity = 1.0;
        }
    }

    // Fixed sky: the same 16 stars every launch. Decorative motion (twinkle,
    // drift) is banned brand-wide, so these are plain rectangles. Coordinates
    // are fractions of the rail's size; the five `big` dots are 2x2 muted,
    // the rest 1x1 outlineSubtle.
    readonly property var starTable: [
        { fx: 0.14, fy: 0.06, big: false }, { fx: 0.82, fy: 0.05, big: true },
        { fx: 0.30, fy: 0.11, big: false }, { fx: 0.60, fy: 0.09, big: false },
        { fx: 0.90, fy: 0.14, big: false }, { fx: 0.08, fy: 0.18, big: true },
        { fx: 0.72, fy: 0.20, big: false }, { fx: 0.22, fy: 0.26, big: false },
        { fx: 0.86, fy: 0.30, big: true }, { fx: 0.12, fy: 0.38, big: false },
        { fx: 0.66, fy: 0.41, big: false }, { fx: 0.34, fy: 0.47, big: true },
        { fx: 0.90, fy: 0.52, big: false }, { fx: 0.15, fy: 0.57, big: false },
        { fx: 0.78, fy: 0.60, big: true }, { fx: 0.45, fy: 0.04, big: false }
    ]

    // ── The scene (decorative: ignored wholesale by accessibility) ──────────
    Item {
        id: scene
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: divider.top
        anchors.bottomMargin: Tokens.s9
        Accessible.ignored: true

        Repeater {
            model: rail.compact ? [] : rail.starTable

            delegate: Rectangle {
                id: star
                required property var modelData
                x: star.modelData.fx * rail.width
                y: star.modelData.fy * rail.height
                width: star.modelData.big ? 2 : 1
                height: width
                color: star.modelData.big ? Theme.muted : Theme.outlineSubtle
            }
        }

        Kit.OrbitArc {
            id: orbit
            visible: !rail.compact
            anchors.horizontalCenter: parent.horizontalCenter
            // Centred on the satellite so the glyph sits on its own orbit.
            y: 64 - height / 2
            rx: 72
            ry: 26
        }

        Kit.BrandGlyph {
            id: satGlyphItem
            glyph: rail.satGlyph
            width: Tokens.glyphXl
            height: Tokens.glyphXl
            anchors.horizontalCenter: parent.horizontalCenter
            // The satellite is far and small: 40px, centre 64 from rail top.
            y: 64 - height / 2

            // The one sanctioned brand-glyph transient: an opacity pulse over
            // the BASE asset (WireLine precedent). Satellite leads at phase 0.
            SequentialAnimation on opacity {
                running: rail.installing && rail.visible && !Tokens.reducedMotion
                loops: Animation.Infinite
                alwaysRunToEnd: true
                NumberAnimation { to: 0.45; duration: Tokens.durBusy / 2 }
                NumberAnimation { to: 1.0; duration: Tokens.durBusy / 2 }
            }
        }

        Kit.BeamLine {
            id: beam
            anchors.horizontalCenter: parent.horizontalCenter
            y: satGlyphItem.y + satGlyphItem.height + Tokens.s6
            height: Math.max(0, dishGlyphItem.y - Tokens.s6 - beam.y)
            width: 2
            live: rail.settled
            transmitting: rail.installing || rail.removing
            upward: rail.removing

            // Packets: payload travelling down the beam, install only —
            // removal has no payload. Hidden entirely under reduced motion.
            Repeater {
                model: 3

                delegate: Rectangle {
                    id: packet
                    required property int index
                    visible: rail.installing && rail.visible && !Tokens.reducedMotion
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: Tokens.s2
                    height: Tokens.s2
                    radius: width / 2
                    color: Theme.primary

                    SequentialAnimation {
                        running: packet.visible

                        // Constant phase offset, once, so the three packets
                        // stay evenly spaced along the beam forever.
                        PauseAnimation { duration: packet.index * Tokens.durBusy * 2 / 3 }
                        SequentialAnimation {
                            loops: Animation.Infinite
                            NumberAnimation {
                                target: packet
                                property: "y"
                                from: 0
                                to: Math.max(0, beam.height - packet.height)
                                duration: Tokens.durBusy * 2
                            }
                        }
                    }
                }
            }
        }

        Kit.BrandGlyph {
            id: dishGlyphItem
            glyph: rail.dishGlyph
            width: Tokens.glyphHero
            height: Tokens.glyphHero
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: ground.top

            // The dish answers the satellite half a period later, so the pulse
            // appears to travel down the beam. While removing the dish pulses
            // alone (the satellite stays plain).
            SequentialAnimation on opacity {
                running: (rail.installing || rail.removing) && rail.visible
                         && !Tokens.reducedMotion
                alwaysRunToEnd: true
                PauseAnimation { duration: Tokens.durBusy / 2 }
                SequentialAnimation {
                    loops: Animation.Infinite
                    NumberAnimation { to: 0.45; duration: Tokens.durBusy / 2 }
                    NumberAnimation { to: 1.0; duration: Tokens.durBusy / 2 }
                }
            }
        }

        Rectangle {
            id: ground
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Tokens.s10
            anchors.rightMargin: Tokens.s10
            anchors.bottom: parent.bottom
            height: 1
            color: Theme.outlineSubtle
        }
    }

    // ── The manifest: facts filling in as answers land ──────────────────────
    Rectangle {
        id: divider
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Tokens.s9
        anchors.rightMargin: Tokens.s9
        anchors.bottom: manifest.top
        anchors.bottomMargin: Tokens.s9
        height: 1
        color: Theme.outlineSubtle
    }

    component ManifestRow: Item {
        id: row

        property string label: ""
        property string value: ""

        readonly property bool answered: row.value.length > 0

        implicitHeight: Math.max(labelText.implicitHeight, valueText.implicitHeight)

        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("%1 — %2").arg(row.label)
                                        .arg(row.answered ? row.value : qsTr("—"))

        Text {
            id: labelText
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            text: row.label
            color: Theme.mutedStrong
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textChip
            font.letterSpacing: Tokens.sectionLetterSpacing
            font.capitalization: Font.AllUppercase
        }

        Text {
            id: valueText
            anchors.left: labelText.right
            anchors.leftMargin: Tokens.s4
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            horizontalAlignment: Text.AlignRight
            text: row.answered ? row.value : qsTr("—")
            elide: Text.ElideMiddle
            color: row.answered ? Theme.onSurface : Theme.mutedStrong
            font.family: Tokens.monoFamily
            font.pixelSize: Tokens.textChip

            // M8: the value "lands" — a single colour flip, no churn.
            Behavior on color {
                enabled: !Tokens.reducedMotion
                ColorAnimation { duration: Tokens.durFast }
            }
        }
    }

    Column {
        id: manifest
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Tokens.s9
        anchors.rightMargin: Tokens.s9
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Tokens.s10
        spacing: Tokens.s3

        ManifestRow { label: qsTr("Version"); value: rail.versionText; width: parent.width }
        ManifestRow { label: qsTr("Destination"); value: rail.destinationText; width: parent.width }
        ManifestRow { label: qsTr("On disk"); value: rail.onDiskText; width: parent.width }
    }

    // The seam to the content column: a subtle hairline, not a full outline —
    // a card edge here would read as a panel instead of a region.
    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.outlineSubtle
    }
}
