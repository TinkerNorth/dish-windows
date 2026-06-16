// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The Qt Quick entry window for the modern Windows chrome (DISH_QML build). A
// frameless ApplicationWindow: the OS draws no title bar, the C++ chrome filter
// supplies snap/resize/Mica, and WindowTitleBar bleeds into the body on the same
// surface so there is no seam between the bar and the content. The body is a
// stub (a centered label) at this migration step — enough to confirm Mica.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Window
import Dish.Chrome

ApplicationWindow {
    id: root
    width: 980
    height: 640
    minimumWidth: 560
    minimumHeight: 380
    visible: true
    title: qsTr("Dish")

    // Frameless: we draw our own chrome. The C++ FramelessWindowChrome filter
    // restores the native snap/resize/shadow that this flag otherwise strips.
    flags: Qt.Window | Qt.FramelessWindowHint

    // When Mica is active the OS backdrop shows through a transparent surface;
    // ChromeBridge.micaActive is set by main.cpp after applyMicaBackdrop(). On
    // pre-Win11 we paint the themed solid background instead.
    color: ChromeBridge.micaActive ? "transparent" : Theme.background

    // The title bar bleeds into the body: same parent, no divider. The bar
    // publishes its caption + maximize-button geometry up to C++ for hit-testing.
    WindowTitleBar {
        id: titleBar
        window: root
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
    }

    // Stub body — a single centered label so Mica is visible behind it.
    Item {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: titleBar.bottom
        anchors.bottom: parent.bottom

        Label {
            anchors.centerIn: parent
            text: qsTr("Dish — Qt Quick chrome preview")
            color: Theme.onSurface
            font.pixelSize: 18
        }
    }
}
