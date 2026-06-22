// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The custom title bar. It is NOT a separate surface — it has no background of
// its own, so it bleeds into the body (Mica or the themed solid). Left: a status
// dot + the "Dish" wordmark. Right: minimize / maximize / close. The whole strip
// is draggable via startSystemMove(); the caption + maximize-button rects are
// pushed to the C++ chrome filter (ChromeBridge) so the native hit-test can
// drive Snap Layouts over the maximize button and let the strip act as caption.

// Bound: the inline CaptionButton component binds `bar.height` (an outer id),
// which qmllint only resolves under bound component behavior.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome

Item {
    id: bar
    height: 40

    // The window we control (set by Main.qml). Used for drag + min/max/close.
    required property var window

    // Publish geometry to C++ whenever the bar or the maximize button moves or
    // resizes. The rects are window-local logical px; C++ scales by DPR.
    function publishRects() {
        ChromeBridge.setCaptionRect(Qt.rect(bar.x, bar.y, bar.width, bar.height));
        var p = maximizeButton.mapToItem(null, 0, 0);
        ChromeBridge.setMaximizeButtonRect(
            Qt.rect(p.x, p.y, maximizeButton.width, maximizeButton.height));
    }

    onWidthChanged: publishRects()
    onHeightChanged: publishRects()
    Component.onCompleted: publishRects()

    // Drag-to-move. We start a native move on press anywhere on the empty strip;
    // the caption buttons sit above this via z-order and consume their own
    // presses, so dragging a button doesn't move the window.
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

    // Left: status dot + wordmark.
    RowLayout {
        anchors.left: parent.left
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Rectangle {
            // implicit*, not width/height: this Rectangle is laid out by the
            // enclosing RowLayout, which owns the final geometry.
            implicitWidth: 8
            implicitHeight: 8
            radius: 4
            color: Theme.success
        }
        Label {
            text: qsTr("Dish")
            color: Theme.onSurface
            font.pixelSize: 13
            font.bold: true
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
            width: 46
            height: bar.height
            property color hoverColor: Theme.surfaceDim
            property color glyphColor: Theme.onSurface
            background: Rectangle {
                color: cb.hovered ? cb.hoverColor : "transparent"
            }
        }

        CaptionButton {
            id: minimizeButton
            contentItem: Canvas {
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = minimizeButton.glyphColor;
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(width / 2 - 5, height / 2);
                    ctx.lineTo(width / 2 + 5, height / 2);
                    ctx.stroke();
                }
            }
            onClicked: bar.window.showMinimized()
        }

        CaptionButton {
            id: maximizeButton
            onXChanged: bar.publishRects()
            onWidthChanged: bar.publishRects()
            contentItem: Canvas {
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = maximizeButton.glyphColor;
                    ctx.lineWidth = 1;
                    ctx.strokeRect(width / 2 - 5, height / 2 - 5, 10, 10);
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
                onPaint: {
                    var ctx = getContext("2d");
                    ctx.reset();
                    ctx.strokeStyle = closeButton.glyphColor;
                    ctx.lineWidth = 1;
                    ctx.beginPath();
                    ctx.moveTo(width / 2 - 5, height / 2 - 5);
                    ctx.lineTo(width / 2 + 5, height / 2 + 5);
                    ctx.moveTo(width / 2 + 5, height / 2 - 5);
                    ctx.lineTo(width / 2 - 5, height / 2 + 5);
                    ctx.stroke();
                }
            }
            onClicked: bar.window.close()
        }
    }
}
