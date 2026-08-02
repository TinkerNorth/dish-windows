// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The wizard's progress affordance: not a numbered breadcrumb but the thing
// being built — pad → wire → host, in Home's own geometry, filling in as the
// answers land, with the three stage markers on a rule beneath. Composed of
// Card (outline, dense) + WireLine + BrandGlyph; only the markers are novel.
//
// Three corrections to the frames are baked in:
//  * A hot-but-empty slot is ACCENT, never amber. Nothing is wrong while the
//    app is looking for a pad — amber is the problem colour everywhere else
//    (stale, Needs pairing, Connecting…, unverified, claim failures), and
//    spending it on "working" is how it stops meaning anything.
//  * Stage 3 owns three of the five pages, so the markers alone stall for 60%
//    of the flow. A three-dot sub-step row rides the right end of the marker
//    rule while stage 3 is active.
//  * A completed marker is clickable. Back is non-destructive, so jumping to an
//    answered stage costs nothing.
//
// The banner costs ~95px of every step, which is why the type table is three
// rows and not seven. `compact` drops the slot sub-lines and the marker labels
// for ~60px on a short window; below Tokens.narrowBreakpoint the wire label
// folds into the host slot's sub-line rather than squeezing the wire.
//
// The slots are PLACEHOLDERS: no hover, no press, no focus ring. They are
// drawn state, not controls. (The dashed edge is drawn here rather than through
// ActionCard.placeholder because a hot slot needs an accent dash and an accent
// wash, which the placeholder mode deliberately does not carry.)

// Bound: the marker and pip delegates read the outer `banner` id alongside
// their required index.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Item {
    id: banner

    // { title: string, sub: string, empty: bool, hot: bool, tone: "accent"|"warn" }
    property var padSlot: ({})
    property var hostSlot: ({})
    // Mono caption over the wire ("as —", "as Xbox 360 · rumble", "binding…").
    property string wireLabel: ""
    // A working link. NEVER true while an apply is in flight — see WireLine.
    property bool live: false
    property bool transmitting: false
    // 1 Input · 2 Destination · 3 Binding.
    property int stage: 1
    // 0 type · 1 feel · 2 review. Only meaningful while stage === 3.
    property int subStep: 0
    property bool compact: false

    // Completed markers only; the page decides what a jump back means.
    signal stageClicked(int stage)

    readonly property string padTitle: (banner.padSlot && banner.padSlot.title !== undefined)
                                       ? banner.padSlot.title : ""
    readonly property string padSub: (banner.padSlot && banner.padSlot.sub !== undefined)
                                     ? banner.padSlot.sub : ""
    readonly property bool padEmpty: (banner.padSlot && banner.padSlot.empty !== undefined)
                                     ? banner.padSlot.empty === true : true
    readonly property bool padHot: (banner.padSlot && banner.padSlot.hot !== undefined)
                                   ? banner.padSlot.hot === true : false
    readonly property string padTone: (banner.padSlot && banner.padSlot.tone !== undefined)
                                      ? banner.padSlot.tone : "accent"

    readonly property string hostTitle: (banner.hostSlot && banner.hostSlot.title !== undefined)
                                        ? banner.hostSlot.title : ""
    readonly property string hostSub: (banner.hostSlot && banner.hostSlot.sub !== undefined)
                                      ? banner.hostSlot.sub : ""
    readonly property bool hostEmpty: (banner.hostSlot && banner.hostSlot.empty !== undefined)
                                      ? banner.hostSlot.empty === true : true
    readonly property bool hostHot: (banner.hostSlot && banner.hostSlot.hot !== undefined)
                                    ? banner.hostSlot.hot === true : false
    readonly property string hostTone: (banner.hostSlot && banner.hostSlot.tone !== undefined)
                                       ? banner.hostSlot.tone : "accent"

    // Below the narrow breakpoint the wire keeps its width and gives up its
    // caption; the caption is a fact, so it moves rather than disappearing.
    readonly property bool narrow: banner.width > 0 && banner.width < Tokens.narrowBreakpoint
    readonly property bool foldLabel: banner.narrow && banner.wireLabel.length > 0
    readonly property string hostSubText: !banner.foldLabel ? banner.hostSub
                                        : banner.hostSub.length > 0
                                          ? banner.hostSub + " · " + banner.wireLabel
                                          : banner.wireLabel

    implicitHeight: frame.implicitHeight

    function markerState(n) {
        if (n < banner.stage)
            return "done";
        if (n === banner.stage)
            return "on";
        return "todo";
    }

    function markerStateLabel(s) {
        if (s === "done")
            return qsTr("done");
        if (s === "on")
            return qsTr("current");
        return qsTr("not started");
    }

    // ── The slot ─────────────────────────────────────────────────────────────
    // One shape, two instances, one file: a slot with a variant would be a kit
    // component of its own, but it has exactly one drawing site and lives
    // inside the only component that composes it.
    component BannerSlot: Item {
        id: slot

        property string title: ""
        property string sub: ""
        property bool empty: true
        property bool hot: false
        property string tone: "accent"
        property bool showSub: true

        readonly property bool warnTone: slot.tone === "warn"
        readonly property color edge: slot.hot ? (slot.empty && slot.warnTone ? Theme.warning
                                                                             : Theme.primary)
                                               : Theme.outline
        readonly property color titleColor: !slot.empty ? Theme.onSurface
                                          : slot.hot ? (slot.warnTone ? Theme.warning
                                                                      : Theme.primary)
                                          : Theme.mutedStrong
        // The design's 7px inset does not exist on the scale; 8 does.
        readonly property int inset: slot.showSub ? Tokens.s4 : Tokens.s1

        implicitHeight: copy.implicitHeight + 2 * slot.inset

        Accessible.role: Accessible.StaticText
        Accessible.name: slot.sub.length > 0 && slot.showSub
                         ? qsTr("%1 — %2").arg(slot.title).arg(slot.sub)
                         : slot.title

        // Ground: the surface of a filled slot, the accent wash of a hot empty
        // one, nothing at all when the slot is cold and empty.
        Rectangle {
            anchors.fill: parent
            radius: Tokens.radiusCard
            color: !slot.empty ? Theme.surface
                 : slot.hot ? Theme.primaryFill
                 : "transparent"
            // An empty slot's edge is the dashed canvas below; a filled slot
            // draws its own hairline.
            border.width: slot.empty ? 0 : 1
            border.color: slot.edge
        }

        // The dashed edge of an unanswered slot. Canvas because Qt 6.7 has no
        // rounded dashed-rect primitive, and Qt.rgba() because a QColor
        // stringifies to #AARRGGBB, which Canvas 2D reads as #RRGGBBAA.
        Canvas {
            id: edgeCanvas
            anchors.fill: parent
            visible: slot.empty

            onPaint: {
                const ctx = getContext("2d");
                ctx.reset();
                const c = slot.edge;
                ctx.strokeStyle = Qt.rgba(c.r, c.g, c.b, c.a);
                ctx.lineWidth = 1;
                ctx.setLineDash([3, 3]);
                ctx.beginPath();
                ctx.roundedRect(0.5, 0.5, width - 1, height - 1,
                                Tokens.radiusCard, Tokens.radiusCard);
                ctx.stroke();
            }

            Connections {
                target: Theme
                function onPaletteChanged() { edgeCanvas.requestPaint(); }
            }
            Connections {
                target: slot
                function onEmptyChanged() { edgeCanvas.requestPaint(); }
                function onHotChanged() { edgeCanvas.requestPaint(); }
                function onToneChanged() { edgeCanvas.requestPaint(); }
            }
        }

        Column {
            id: copy
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.leftMargin: Tokens.s5
            anchors.rightMargin: Tokens.s5
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.s1

            Text {
                width: parent.width
                text: slot.title
                elide: Text.ElideRight
                font.pixelSize: Tokens.textSummary
                font.weight: Font.DemiBold
                color: slot.titleColor
            }
            Text {
                visible: slot.showSub && slot.sub.length > 0
                width: parent.width
                text: slot.sub
                elide: Text.ElideRight
                font.pixelSize: Tokens.textChip
                color: Theme.mutedStrong
            }
        }
    }

    // ── The banner ───────────────────────────────────────────────────────────
    Card {
        id: frame
        anchors.fill: parent
        filled: false
        dense: true

        contentItem: ColumnLayout {
            spacing: 0

            RowLayout {
                id: slotRow
                spacing: Tokens.s5
                Layout.fillWidth: true

                BannerSlot {
                    title: banner.padTitle
                    sub: banner.padSub
                    empty: banner.padEmpty
                    hot: banner.padHot
                    tone: banner.padTone
                    showSub: !banner.compact
                    Layout.preferredWidth: 232
                    Layout.minimumWidth: 160
                    Layout.alignment: Qt.AlignVCenter
                }

                WireLine {
                    live: banner.live
                    transmitting: banner.transmitting
                    // Folded into the host slot's sub-line on a narrow window.
                    label: banner.foldLabel ? "" : banner.wireLabel
                    Layout.fillWidth: true
                    Layout.minimumWidth: 60
                    Layout.alignment: Qt.AlignVCenter
                }

                BannerSlot {
                    title: banner.hostTitle
                    sub: banner.hostSubText
                    empty: banner.hostEmpty
                    hot: banner.hostHot
                    tone: banner.hostTone
                    showSub: !banner.compact
                    Layout.preferredWidth: 232
                    Layout.minimumWidth: 160
                    Layout.alignment: Qt.AlignVCenter
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                Layout.topMargin: banner.compact ? Tokens.s2 : Tokens.s4
                color: Theme.outlineSubtle
            }

            RowLayout {
                id: markerRow
                spacing: Tokens.s5
                Layout.fillWidth: true
                Layout.topMargin: banner.compact ? Tokens.s2 : Tokens.s4

                Repeater {
                    model: [qsTr("Input"), qsTr("Destination"), qsTr("Binding")]

                    delegate: AbstractButton {
                        id: marker

                        required property int index
                        required property string modelData

                        readonly property int stageNumber: marker.index + 1
                        readonly property string markerState: banner.markerState(marker.stageNumber)
                        readonly property bool completed: marker.markerState === "done"
                        readonly property bool current: marker.markerState === "on"

                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter

                        // Only an answered stage is reachable. A not-yet stage is
                        // information, so it keeps full opacity and says its
                        // state in colour — the 0.55 rule is for dead controls.
                        enabled: marker.completed
                        opacity: 1.0
                        focusPolicy: marker.completed ? Qt.StrongFocus : Qt.NoFocus
                        hoverEnabled: marker.completed

                        Accessible.role: Accessible.Button
                        Accessible.name: qsTr("Step %1, %2").arg(marker.stageNumber)
                                                            .arg(marker.modelData)
                        Accessible.description: banner.markerStateLabel(marker.markerState)

                        HoverHandler {
                            enabled: marker.completed
                            cursorShape: Qt.PointingHandCursor
                        }

                        onClicked: banner.stageClicked(marker.stageNumber)

                        // The one global focus treatment, outside the bounds and
                        // on visualFocus only so a mouse press never rings.
                        background: Item {
                            Rectangle {
                                anchors.fill: parent
                                anchors.margins: -Tokens.s1
                                radius: Tokens.radiusButton
                                visible: marker.visualFocus
                                color: "transparent"
                                border.width: 2
                                border.color: Theme.focusRing
                            }
                        }

                        contentItem: RowLayout {
                            spacing: Tokens.s3

                            Rectangle {
                                id: circle
                                Layout.preferredWidth: 16
                                Layout.preferredHeight: 16
                                Layout.alignment: Qt.AlignVCenter
                                radius: width / 2
                                color: marker.current ? Theme.primary
                                     : marker.completed ? Theme.primaryFill
                                     : "transparent"
                                border.width: marker.markerState === "todo" ? 1 : 0
                                border.color: Theme.outline

                                Text {
                                    anchors.centerIn: parent
                                    text: marker.completed ? "✓" : String(marker.stageNumber)
                                    font.pixelSize: Tokens.textChip
                                    font.weight: marker.current ? Font.Bold : Font.Normal
                                    color: marker.current ? Theme.onPrimary
                                         : marker.completed ? Theme.primary
                                         : Theme.mutedStrong
                                }
                            }

                            Text {
                                visible: !banner.compact
                                text: marker.modelData
                                elide: Text.ElideRight
                                font.family: Tokens.monoFamily
                                font.pixelSize: Tokens.textChip
                                font.letterSpacing: Tokens.sectionLetterSpacing
                                font.capitalization: Font.AllUppercase
                                color: marker.current ? Theme.primary
                                     : marker.completed ? Theme.onSurface
                                     : Theme.mutedStrong
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }
                }

                // The sub-step pips: the only progress signal stage 3 has.
                Row {
                    id: subDots
                    visible: banner.stage === 3
                    spacing: Tokens.s1
                    Layout.alignment: Qt.AlignVCenter | Qt.AlignRight

                    Accessible.role: Accessible.StaticText
                    Accessible.name: qsTr("Sub-step %1 of 3").arg(banner.subStep + 1)

                    Repeater {
                        model: 3

                        delegate: Rectangle {
                            id: pip
                            required property int index

                            width: Tokens.s3
                            height: Tokens.s3
                            radius: width / 2
                            color: pip.index <= banner.subStep ? Theme.primary : "transparent"
                            border.width: 1
                            border.color: pip.index <= banner.subStep ? Theme.primary
                                                                      : Theme.outline
                        }
                    }
                }
            }
        }
    }
}
