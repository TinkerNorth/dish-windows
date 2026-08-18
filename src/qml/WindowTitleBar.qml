// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The custom title bar. NOT a separate surface: it has no background of its
// own, so the app bleeds into it. The hamburger cell is exactly the collapsed
// rail width, so the icon column reads as one continuous strip. Its rects go to
// the C++ chrome filter, which drives the native hit-test (drag, Snap Layouts).

// Bound: the inline CaptionButton component binds `bar.height`, an outer id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome
import "kit" as Kit

Item {
    id: bar
    height: Tokens.titleBarHeight

    required property var window

    // Rects are window-local logical px; C++ scales by DPR. The hamburger,
    // minimize and close rects are CLIENT CARVE-OUTS: without them the whole
    // strip native-resolves to HTCAPTION and a press starts a system drag, so
    // those buttons never receive a click.
    function publishRects() {
        ChromeBridge.setCaptionRect(Qt.rect(bar.x, bar.y, bar.width, bar.height));
        var p = maximizeButton.mapToItem(null, 0, 0);
        ChromeBridge.setMaximizeButtonRect(
            Qt.rect(p.x, p.y, maximizeButton.width, maximizeButton.height));
        var m = minimizeButton.mapToItem(null, 0, 0);
        ChromeBridge.setMinimizeButtonRect(
            Qt.rect(m.x, m.y, minimizeButton.width, minimizeButton.height));
        var c = closeButton.mapToItem(null, 0, 0);
        ChromeBridge.setCloseButtonRect(Qt.rect(c.x, c.y, closeButton.width, closeButton.height));
        var g = hamburger.mapToItem(null, 0, 0);
        ChromeBridge.setLeftClientRect(Qt.rect(g.x, g.y, hamburger.width, hamburger.height));
        // An EMPTY rect while the pill is hidden, which is most of the app's
        // life: no update means no carve-out at all.
        if (updatePill.visible) {
            var u = updatePill.mapToItem(null, 0, 0);
            ChromeBridge.setUpdatePillRect(
                Qt.rect(u.x, u.y, updatePill.width, updatePill.height));
        } else {
            ChromeBridge.setUpdatePillRect(Qt.rect(0, 0, 0, 0));
        }
    }

    onWidthChanged: publishRects()
    onHeightChanged: publishRects()
    Component.onCompleted: publishRects()

    // The hamburger and caption buttons sit above this in z and consume their
    // own presses, so clicking one doesn't start a window move.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: bar.window.startSystemMove()
        onDoubleClicked: bar.toggleMaximize()
    }

    function toggleMaximize() {
        if (bar.window.visibility === Window.Maximized)
            bar.window.showNormal();
        else
            bar.window.showMaximized();
    }

    Row {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: 1

        AbstractButton {
            id: hamburger
            width: Tokens.railCompact
            height: bar.height

            onClicked: App.setRailCollapsed(!App.railCollapsed)

            background: Rectangle {
                color: hamburger.hovered ? Theme.primaryHover : "transparent"
            }
            // Canvas 2D parses a stringified colour as #RRGGBBAA, so only an
            // OPAQUE Theme role may be handed to it this way — every role used
            // in this file (onSurface, onPrimary) is opaque by construction.
            // Anything else must go through Qt.rgba() (kit rule C5).
            contentItem: Canvas {
                id: hamburgerCanvas
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = String(Theme.onSurface);
                    ctx.lineWidth = 1.6;
                    ctx.lineCap = "round";
                    var cx = width / 2, cy = height / 2;
                    ctx.beginPath();
                    ctx.moveTo(cx - 5.5, cy - 4); ctx.lineTo(cx + 5.5, cy - 4);
                    ctx.moveTo(cx - 5.5, cy);     ctx.lineTo(cx + 5.5, cy);
                    ctx.moveTo(cx - 5.5, cy + 4); ctx.lineTo(cx + 5.5, cy + 4);
                    ctx.stroke();
                }
                // Repaint when the palette flips (Canvas doesn't rebind colors).
                Connections {
                    target: Theme
                    function onPaletteChanged() { hamburgerCanvas.requestPaint(); }
                }
            }

            // Declared, never attached — see DishToolTip in QML_UI_KIT.md.
            Kit.DishToolTip {
                id: hamburgerTip
                visible: hamburger.hovered
                delay: 800
                text: App.railCollapsed ? qsTr("Expand navigation")
                                        : qsTr("Collapse navigation")
                y: hamburger.height + Tokens.s2
            }
        }

        Item { width: 0; height: 1 }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.s4

            Kit.AppMark {
                width: Tokens.glyphSm
                height: Tokens.glyphSm
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                text: qsTr("Dish")
                color: Theme.onSurface
                font.pixelSize: Tokens.textSummary
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    Row {
        id: captionButtons
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: 1

        component CaptionButton: AbstractButton {
            id: cb
            width: Tokens.captionButtonWidth
            height: bar.height
            property color hoverColor: Theme.primaryHover
            // Hover this button cannot feel for itself. Only maximize needs it
            // (it is non-client, see below), but the fill has to be ONE
            // expression or the three buttons would light differently.
            property bool nativeHovered: false
            readonly property color glyphColor: Theme.onSurface
            background: Rectangle {
                color: (cb.hovered || cb.nativeHovered) ? cb.hoverColor : "transparent"
            }
        }

        // FIRST in the row, left of minimize: the pill is app state, not a
        // window command, so it must not sit inside the min/max/close cluster.
        // It collapses to zero width whenever there is no update, and every
        // geometry move re-publishes the carve-out (the Row shifts the caption
        // buttons along with it).
        UpdatePill {
            id: updatePill
            width: Tokens.captionButtonWidth * 0.75
            height: bar.height
            onVisibleChanged: bar.publishRects()
            onXChanged: bar.publishRects()
            onWidthChanged: bar.publishRects()
        }

        CaptionButton {
            id: minimizeButton
            contentItem: Canvas {
                id: minimizeCanvas
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = String(minimizeButton.glyphColor);
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(width / 2 - 5, height / 2);
                    ctx.lineTo(width / 2 + 5, height / 2);
                    ctx.stroke();
                }
                Connections {
                    target: Theme
                    function onPaletteChanged() { minimizeCanvas.requestPaint(); }
                }
            }
            onClicked: bar.window.showMinimized()
        }

        CaptionButton {
            id: maximizeButton
            onXChanged: bar.publishRects()
            onWidthChanged: bar.publishRects()

            // NON-CLIENT: the chrome filter answers HTMAXBUTTON over it so Win11
            // opens the Snap Layouts flyout, and Quick is never told about a
            // non-client pointer. Its own `hovered` is therefore always false —
            // without the native tracker this button would never light.
            nativeHovered: ChromeBridge.maximizeHovered

            // Maximise and restore are the same button, so the glyph is the only
            // signal the click landed. Canvas doesn't rebind: repaint on flip.
            readonly property bool zoomed: bar.window.visibility === Window.Maximized
            onZoomedChanged: maximizeCanvas.requestPaint()

            contentItem: Canvas {
                id: maximizeCanvas
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = String(maximizeButton.glyphColor);
                    ctx.lineWidth = 1;
                    var cx = width / 2, cy = height / 2;
                    if (maximizeButton.zoomed) {
                        // Front pane, then the two exposed edges of the one behind.
                        ctx.strokeRect(cx - 5, cy - 3, 8, 8);
                        ctx.beginPath();
                        ctx.moveTo(cx - 3, cy - 5);
                        ctx.lineTo(cx + 5, cy - 5);
                        ctx.lineTo(cx + 5, cy + 3);
                        ctx.stroke();
                    } else {
                        ctx.strokeRect(cx - 5, cy - 5, 10, 10);
                    }
                }
                Connections {
                    target: Theme
                    function onPaletteChanged() { maximizeCanvas.requestPaint(); }
                }
            }
            // The real press runs in C++: FramelessWindowChrome takes the
            // WM_NCLBUTTONDOWN/UP pair and posts SC_MAXIMIZE / SC_RESTORE
            // itself, because DefWindowProc only tracks caption buttons on a
            // window that HAS a caption. Nothing reaches this handler today; it
            // is the client-path fallback and what keyboard activation uses.
            onClicked: bar.toggleMaximize()
        }

        CaptionButton {
            id: closeButton
            hoverColor: Theme.error
            contentItem: Canvas {
                id: closeCanvas
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    // On-accent ink over the red hover fill; themed otherwise.
                    ctx.strokeStyle = closeButton.hovered ? String(Theme.onPrimary)
                                                          : String(closeButton.glyphColor);
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(width / 2 - 5, height / 2 - 5);
                    ctx.lineTo(width / 2 + 5, height / 2 + 5);
                    ctx.moveTo(width / 2 + 5, height / 2 - 5);
                    ctx.lineTo(width / 2 - 5, height / 2 + 5);
                    ctx.stroke();
                }
                Connections {
                    target: Theme
                    function onPaletteChanged() { closeCanvas.requestPaint(); }
                }
                Connections {
                    target: closeButton
                    function onHoveredChanged() { closeCanvas.requestPaint(); }
                }
            }
            onClicked: bar.window.close()
        }
    }
}
