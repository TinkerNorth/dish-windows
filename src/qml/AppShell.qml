// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The NavigationView-style app shell: a left navigation rail (Controllers /
// Connections / Settings) and a StackView content area with back/breadcrumb
// support. It sits UNDER the WindowTitleBar and ON the Mica surface — every
// background here is transparent (or a Card surface) so the OS backdrop shows
// through; nothing paints an opaque full-window panel. Selecting a rail item
// resets the StackView to that destination's root page. Pages can push detail
// pages onto the StackView; the breadcrumb's back affordance pops them.

// Bound component behavior: lets the Repeater delegate reference the shell's id
// and its `required` model props without the engine falling back to dynamic
// (unqualified) lookup — keeps binding resolution static and qmllint quiet.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import "kit" as Kit

Item {
    id: shell

    // The destinations. `source` is the page component file the StackView roots
    // to when the rail item is chosen. Order = rail order.
    readonly property var destinations: [
        { key: "controllers", label: qsTr("Controllers"), glyph: "satellite",           source: "pages/ControllersPage.qml" },
        { key: "connections", label: qsTr("Connections"), glyph: "satellite-broadcasting", source: "pages/ConnectionsPage.qml" },
        { key: "settings",    label: qsTr("Settings"),    glyph: "dish",                source: "pages/SettingsPage.qml" }
    ]

    property int currentIndex: 0

    // The breadcrumb heading. The root of each destination shows the rail label;
    // a pushed detail page supplies its own title via pushDetail(url, title) so
    // we never read an untyped `title` off the StackView's QQuickItem.
    property string currentTitle: destinations[0].label

    function selectDestination(index) {
        if (index === shell.currentIndex && contentStack.depth <= 1)
            return;                       // already on that root; nothing to do
        shell.currentIndex = index;
        shell.currentTitle = shell.destinations[index].label;
        // replace() clears any pushed detail pages — a rail switch is a fresh root.
        contentStack.replace(null, Qt.resolvedUrl(shell.destinations[index].source));
    }

    // Pages push a detail view with an explicit breadcrumb title; pop() (the
    // Back button) restores the destination root and its label.
    function pushDetail(url, title) {
        shell.currentTitle = title;
        contentStack.push(Qt.resolvedUrl(url));
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Left navigation rail. Transparent so Mica shows; a hairline
        // outline on the right edge separates it from the content. ----
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 200
            color: "transparent"

            Rectangle {                   // right-edge hairline divider
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.outline
                opacity: 0.5
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 12
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 4

                Repeater {
                    model: shell.destinations
                    delegate: ItemDelegate {
                        id: railItem
                        required property int index
                        required property var modelData
                        readonly property bool active: railItem.index === shell.currentIndex
                        Layout.fillWidth: true
                        implicitHeight: 40

                        onClicked: shell.selectDestination(railItem.index)

                        background: Rectangle {
                            radius: 8
                            color: railItem.active
                                   ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.16)
                                   : railItem.hovered ? Qt.rgba(Theme.onSurface.r, Theme.onSurface.g, Theme.onSurface.b, 0.06)
                                   : "transparent"

                            Rectangle {   // active-destination accent bar
                                visible: railItem.active
                                anchors.left: parent.left
                                anchors.leftMargin: 2
                                anchors.verticalCenter: parent.verticalCenter
                                width: 3
                                height: 18
                                radius: 1.5
                                color: Theme.primary
                            }
                        }

                        contentItem: RowLayout {
                            spacing: 10
                            Kit.BrandGlyph {
                                glyph: railItem.modelData.glyph
                                Layout.preferredWidth: 18
                                Layout.preferredHeight: 18
                            }
                            Label {
                                text: railItem.modelData.label
                                color: railItem.active ? Theme.onSurface : Theme.muted
                                font.pixelSize: 13
                                font.bold: railItem.active
                                Layout.fillWidth: true
                            }
                        }
                    }
                }

                Item { Layout.fillHeight: true }   // push rail items to the top
            }
        }

        // ---- Content column: breadcrumb header + StackView. ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Breadcrumb / back header. Shows a back chevron only when a detail
            // page is pushed (depth > 1); otherwise just the destination name.
            RowLayout {
                Layout.fillWidth: true
                Layout.leftMargin: 24
                Layout.rightMargin: 24
                Layout.topMargin: 12
                Layout.bottomMargin: 4
                spacing: 8

                Kit.OutlineButton {
                    text: qsTr("‹ Back")
                    visible: contentStack.depth > 1
                    implicitHeight: 28
                    onClicked: {
                        contentStack.pop();
                        // Back to the destination root: restore its rail label.
                        shell.currentTitle = shell.destinations[shell.currentIndex].label;
                    }
                }
                Label {
                    // The page's own `title` (Kit.Page sets it). Read via a string
                    // subscript, not a typed-property access, so qmllint doesn't
                    // flag `title` as missing on the StackView's QQuickItem.
                    text: shell.currentTitle
                    color: Theme.onSurface
                    font.pixelSize: 18
                    font.bold: true
                    Layout.fillWidth: true
                }
            }

            // The content host. Transparent: pages sit on Mica. Initial item is
            // the first destination's root page.
            StackView {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                initialItem: Qt.resolvedUrl(shell.destinations[0].source)
                background: null
                // Confine the slide transition to the content bounds so an
                // outgoing page disappears at the rail divider (slides "behind"
                // it) instead of painting over the transparent rail to the edge.
                clip: true
            }
        }
    }
}
