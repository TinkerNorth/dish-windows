// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer's custom title bar: WindowTitleBar reduced to what the
// one-screen window needs — the app mark, the title, minimize and close.
// There is no maximize cell and no rect is published for one, so the chrome
// filter never answers HTMAXBUTTON and Snap Layouts stays out of a window
// that only edge-resizes. Its rects go to ChromeBridge, which drives the
// native hit-test (caption drag, resize borders, client carve-outs for
// minimize/close). Glyphs are composed from Rectangles, not Canvas:
// src/qml/setup is a literal-scanner STRICT directory and Canvas drawing is
// a kit-only privilege.

// Bound: the inline CaptionButton component binds `bar.height`, an outer id.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import Dish.Chrome
import Dish.Chrome as Kit

Item {
    id: bar
    height: Tokens.titleBarHeight

    required property var window
    property string titleText: ""

    // Rects are window-local logical px; C++ scales by DPR. Minimize and close
    // are CLIENT CARVE-OUTS: without them the whole strip native-resolves to
    // HTCAPTION and a press starts a system drag, so those buttons never
    // receive a click.
    function publishRects() {
        ChromeBridge.setCaptionRect(Qt.rect(bar.x, bar.y, bar.width, bar.height));
        var m = minimizeButton.mapToItem(null, 0, 0);
        ChromeBridge.setMinimizeButtonRect(
            Qt.rect(m.x, m.y, minimizeButton.width, minimizeButton.height));
        var c = closeButton.mapToItem(null, 0, 0);
        ChromeBridge.setCloseButtonRect(Qt.rect(c.x, c.y, closeButton.width, closeButton.height));
    }

    onWidthChanged: bar.publishRects()
    onHeightChanged: bar.publishRects()
    Component.onCompleted: bar.publishRects()

    // The caption buttons sit above this in z and consume their own presses,
    // so clicking one doesn't start a window move. No double-click handler:
    // the window has no maximize to toggle.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: bar.window.startSystemMove()
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Tokens.s10
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: Tokens.s4
        z: 1

        Kit.AppMark {
            width: Tokens.glyphSm
            height: Tokens.glyphSm
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: bar.titleText
            color: Theme.onSurface
            font.pixelSize: Tokens.textSummary
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
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
            readonly property color glyphColor: Theme.onSurface
            background: Rectangle {
                color: cb.hovered ? cb.hoverColor : "transparent"
            }
        }

        CaptionButton {
            id: minimizeButton
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Minimize")
            contentItem: Item {
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 1
                    color: minimizeButton.glyphColor
                }
            }
            onClicked: bar.window.showMinimized()
        }

        CaptionButton {
            id: closeButton
            hoverColor: Theme.error
            readonly property color inkColor: closeButton.hovered ? Theme.onPrimary
                                                                  : closeButton.glyphColor
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("Close")
            contentItem: Item {
                // The X: two 1px bars rotated ±45°, antialiased. 14px covers
                // the 10px glyph box diagonal.
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 1
                    rotation: 45
                    antialiasing: true
                    color: closeButton.inkColor
                }
                Rectangle {
                    anchors.centerIn: parent
                    width: 14
                    height: 1
                    rotation: -45
                    antialiasing: true
                    color: closeButton.inkColor
                }
            }
            onClicked: bar.window.close()
        }
    }
}
