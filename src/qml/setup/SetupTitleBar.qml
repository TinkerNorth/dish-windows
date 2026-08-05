// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The installer's custom title bar: WindowTitleBar with the hamburger cell
// removed (there is no rail to collapse) and zero App coupling. Its rects go
// to ChromeBridge, which drives the native hit-test (caption drag, Snap
// Layouts over maximize, client carve-outs for minimize/close). Unlike the
// app's bar the glyphs are composed from Rectangles, not Canvas: src/qml/setup
// is a literal-scanner STRICT directory and Canvas drawing is a kit-only
// privilege.

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
    // receive a click. No left client rect: there is no hamburger here.
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
    }

    onWidthChanged: bar.publishRects()
    onHeightChanged: bar.publishRects()
    Component.onCompleted: bar.publishRects()

    function toggleMaximize() {
        if (bar.window.visibility === Window.Maximized)
            bar.window.showNormal();
        else
            bar.window.showMaximized();
    }

    // The caption buttons sit above this in z and consume their own presses,
    // so clicking one doesn't start a window move.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        onPressed: bar.window.startSystemMove()
        onDoubleClicked: bar.toggleMaximize()
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: Tokens.s10
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        spacing: Tokens.s4
        z: 1

        Kit.BrandGlyph {
            glyph: "dish"
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
            // Hover this button cannot feel for itself. Only maximize needs it
            // (non-client, see below), but the fill has to be ONE expression
            // or the three buttons would light differently.
            property bool nativeHovered: false
            readonly property color glyphColor: Theme.onSurface
            background: Rectangle {
                color: (cb.hovered || cb.nativeHovered) ? cb.hoverColor : "transparent"
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
            id: maximizeButton
            onXChanged: bar.publishRects()
            onWidthChanged: bar.publishRects()

            // NON-CLIENT: the chrome filter answers HTMAXBUTTON over it so
            // Win11 opens the Snap Layouts flyout, and Quick is never told
            // about a non-client pointer. Its own `hovered` is therefore
            // always false — without the native tracker it would never light.
            nativeHovered: ChromeBridge.maximizeHovered

            readonly property bool zoomed: bar.window.visibility === Window.Maximized

            Accessible.role: Accessible.Button
            Accessible.name: maximizeButton.zoomed ? qsTr("Restore") : qsTr("Maximize")

            contentItem: Item {
                // Maximize: one 10px outlined pane. Restore: an 8px front pane
                // plus the two exposed edges of the one behind it.
                Rectangle {
                    visible: !maximizeButton.zoomed
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    color: "transparent"
                    border.width: 1
                    border.color: maximizeButton.glyphColor
                }
                Item {
                    visible: maximizeButton.zoomed
                    anchors.centerIn: parent
                    width: 12
                    height: 12

                    Rectangle { // back pane, top edge
                        x: 3
                        y: 0
                        width: 9
                        height: 1
                        color: maximizeButton.glyphColor
                    }
                    Rectangle { // back pane, right edge
                        x: 11
                        y: 0
                        width: 1
                        height: 9
                        color: maximizeButton.glyphColor
                    }
                    Rectangle { // front pane
                        x: 0
                        y: 3
                        width: 9
                        height: 9
                        color: "transparent"
                        border.width: 1
                        border.color: maximizeButton.glyphColor
                    }
                }
            }
            // The real press runs in C++: FramelessWindowChrome takes the
            // WM_NCLBUTTONDOWN/UP pair and posts SC_MAXIMIZE / SC_RESTORE
            // itself. Nothing reaches this handler today; it is the
            // client-path fallback and what keyboard activation uses.
            onClicked: bar.toggleMaximize()
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
