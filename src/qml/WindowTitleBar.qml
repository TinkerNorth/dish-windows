// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The custom title bar (V1 Fluent, 44px). It is NOT a separate surface — it has
// no background of its own, so the app bleeds into it (Mica or the themed
// solid). Left: the rail hamburger (its 48px cell width == the collapsed rail,
// so the icon column reads as one continuous strip), then the Dish mark +
// wordmark. Right: minimize / maximize / close. The whole strip is draggable
// via startSystemMove(); the caption + maximize-button rects are pushed to the
// C++ chrome filter (ChromeBridge) so the native hit-test can drive Snap
// Layouts over the maximize button and let the strip act as caption.

// Bound: the inline CaptionButton component binds `bar.height` (an outer id),
// which qmllint only resolves under bound component behavior.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import "kit" as Kit

Item {
    id: bar
    height: Tokens.titleBarHeight

    // The window we control (set by Main.qml). Used for drag + min/max/close.
    required property var window

    // Publish geometry to C++ whenever the bar or its buttons move or resize.
    // The rects are window-local logical px; C++ scales by DPR. The hamburger
    // + minimize + close rects are CLIENT CARVE-OUTS — without them the whole
    // strip native-resolves to HTCAPTION and a press starts a system drag, so
    // those buttons never receive a click (the launch-day symptom).
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
    }

    onWidthChanged: publishRects()
    onHeightChanged: publishRects()
    Component.onCompleted: publishRects()

    // Drag-to-move. We start a native move on press anywhere on the empty strip;
    // the hamburger + caption buttons sit above this via z-order and consume
    // their own presses, so clicking a button doesn't move the window.
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

    // Left: the rail hamburger + the mark + wordmark.
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
                color: hamburger.hovered ? Qt.rgba(230 / 255, 236 / 255, 1, 0.08) : "transparent"
            }
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

            ToolTip.visible: hovered
            ToolTip.delay: 800
            ToolTip.text: App.railCollapsed ? qsTr("Expand navigation") : qsTr("Collapse navigation")
        }

        Item { width: 0; height: 1 }

        Row {
            anchors.verticalCenter: parent.verticalCenter
            spacing: Tokens.s4

            Kit.BrandGlyph {
                glyph: "dish"
                width: 16
                height: 16
                anchors.verticalCenter: parent.verticalCenter
            }
            Label {
                text: qsTr("Dish")
                color: Theme.onSurface
                font.pixelSize: 12
                font.weight: Font.DemiBold
                anchors.verticalCenter: parent.verticalCenter
            }
        }
    }

    // Right: caption buttons. Above the drag MouseArea in z so they take presses.
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
            property color hoverColor: Qt.rgba(230 / 255, 236 / 255, 1, 0.08)
            // The glyph tone; close swaps to white on its red hover.
            readonly property color glyphColor: Theme.onSurface
            background: Rectangle {
                color: cb.hovered ? cb.hoverColor : "transparent"
            }
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
            contentItem: Canvas {
                id: maximizeCanvas
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = String(maximizeButton.glyphColor);
                    ctx.lineWidth = 1;
                    ctx.strokeRect(width / 2 - 5, height / 2 - 5, 10, 10);
                }
                Connections {
                    target: Theme
                    function onPaletteChanged() { maximizeCanvas.requestPaint(); }
                }
            }
            // Win11 sends a synthetic NCLBUTTONUP -> we still wire click as a
            // fallback for when snap isn't involved.
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
                    // White X on the red hover fill (the ds cap-close rule);
                    // themed otherwise.
                    ctx.strokeStyle = closeButton.hovered ? "#ffffff"
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
