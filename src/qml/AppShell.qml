// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The app shell (V1 Fluent): a COLLAPSIBLE left rail (Controllers /
// Connections up top, Settings pinned to the footer with the version line) and
// a content column with a per-page header (back chevron · title · pill ·
// dot+sub line) over a StackView. Collapsed, the rail is a 48px icon strip
// continuing the title bar's hamburger cell; expanded it is a 236px labeled
// pane. The state persists via App.railCollapsed (the hamburger toggles it).
//
// Overlays are IN-SCENE: dialogs are Kit.ContentDialog popups over a scrim and
// transient errors land in the one toast host below — no extra OS windows.
//
// Per-page header contract: a page (or pushed detail) may declare
//   readonly property string headerTitle / headerSub / headerDot / headerPill
// and the shell renders them; absent properties fall back to the rail label /
// empty. Pages reach the shell (destination switching, detail push) through
// `StackView.view.shellApi`.

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
    // to when the rail item is chosen. The first two render at the rail top;
    // the last (Settings) is pinned to the rail footer per the design.
    readonly property var destinations: [
        { key: "controllers", label: qsTr("Controllers"), glyph: "satellite",               source: "pages/ControllersPage.qml" },
        { key: "connections", label: qsTr("Connections"), glyph: "satellite-broadcasting",  source: "pages/ConnectionsPage.qml" },
        { key: "settings",    label: qsTr("Settings"),    glyph: "dish",                    source: "pages/SettingsPage.qml" }
    ]

    property int currentIndex: 0
    readonly property bool collapsed: App.railCollapsed

    // The header title fallback. The root of each destination shows the rail
    // label unless the page supplies headerTitle; a pushed detail supplies its
    // own via pushDetail(url, title) or its headerTitle property.
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
    // back chevron) restores the destination root and its label.
    function pushDetail(url, title) {
        shell.currentTitle = title;
        contentStack.push(Qt.resolvedUrl(url));
    }

    // What the header renders for the current page: the page's own header*
    // properties when declared, else the fallbacks.
    readonly property var _cur: contentStack.currentItem
    readonly property string headerTitle: _cur && _cur.headerTitle !== undefined
                                          ? _cur.headerTitle : currentTitle
    readonly property string headerSub: _cur && _cur.headerSub !== undefined ? _cur.headerSub : ""
    readonly property string headerDot: _cur && _cur.headerDot !== undefined ? _cur.headerDot : ""
    readonly property string headerPill: _cur && _cur.headerPill !== undefined
                                         ? _cur.headerPill : ""

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ---- Left navigation rail (design: a solid surface pane with a
        // hairline right edge; 236px labeled / 48px icon strip). ----
        Rectangle {
            id: rail
            Layout.fillHeight: true
            Layout.preferredWidth: shell.collapsed ? Tokens.railCompact : Tokens.railExpanded
            color: Theme.surface

            Behavior on Layout.preferredWidth {
                NumberAnimation { duration: 140; easing.type: Easing.OutCubic }
            }

            Rectangle {                   // right-edge hairline divider
                anchors.right: parent.right
                width: 1
                height: parent.height
                color: Theme.outline
            }

            // One rail item; used for the top destinations and the pinned
            // Settings entry.
            component RailItem: ItemDelegate {
                id: railItem
                required property int destIndex
                readonly property var dest: shell.destinations[destIndex]
                readonly property bool active: destIndex === shell.currentIndex

                implicitHeight: Tokens.navItemHeight
                // The content is positioned in absolute cell coordinates below;
                // control padding would double-inset it.
                padding: 0

                onClicked: shell.selectDestination(destIndex)

                background: Rectangle {
                    radius: 5
                    color: railItem.active ? Theme.primaryFill
                         : railItem.hovered ? Qt.rgba(230 / 255, 236 / 255, 1, 0.06)
                         : "transparent"

                    Rectangle {   // active-destination accent bar, hugging the edge
                        visible: railItem.active
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 3
                        height: 16
                        radius: 2
                        color: Theme.primary
                    }
                }

                contentItem: Item {
                    // Fixed icon cell (railCompact minus the item margins) so the
                    // glyph column doesn't shift as the rail animates.
                    Kit.BrandGlyph {
                        glyph: railItem.dest.glyph
                        width: 18
                        height: 18
                        anchors.verticalCenter: parent.verticalCenter
                        x: (Tokens.railCompact - 8) / 2 - width / 2
                    }
                    Label {
                        text: railItem.dest.label
                        color: railItem.active ? Theme.onSurface : Theme.muted
                        font.pixelSize: Tokens.textBase
                        font.weight: railItem.active ? Font.DemiBold : Font.Normal
                        anchors.verticalCenter: parent.verticalCenter
                        x: Tokens.railCompact - 8
                        opacity: shell.collapsed ? 0 : 1
                        visible: opacity > 0
                        Behavior on opacity { NumberAnimation { duration: 100 } }
                    }
                }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.topMargin: 4
                anchors.bottomMargin: 8
                anchors.leftMargin: 4
                anchors.rightMargin: 4
                spacing: 0

                RailItem { destIndex: 0; Layout.fillWidth: true; Layout.margins: 2 }
                RailItem { destIndex: 1; Layout.fillWidth: true; Layout.margins: 2 }

                Item { Layout.fillHeight: true }   // pin Settings to the footer

                Rectangle {
                    Layout.fillWidth: true
                    Layout.leftMargin: 4
                    Layout.rightMargin: 4
                    Layout.topMargin: 4
                    Layout.bottomMargin: 4
                    implicitHeight: 1
                    color: Theme.outline
                }

                RailItem { destIndex: 2; Layout.fillWidth: true; Layout.margins: 2 }

                Label {
                    text: qsTr("Dish %1").arg(App.appVersion)
                    font.family: Tokens.monoFamily
                    font.pixelSize: 10
                    color: Theme.muted
                    Layout.leftMargin: 8
                    Layout.topMargin: 6
                    Layout.bottomMargin: 2
                    opacity: shell.collapsed ? 0 : 1
                    visible: opacity > 0
                    Behavior on opacity { NumberAnimation { duration: 100 } }
                }
            }
        }

        // ---- Content column: per-page header + StackView. ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // Header: back ‹ (when a detail is pushed) · title · pill, then the
            // dot + sub line underneath.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.leftMargin: Tokens.pagePadding
                Layout.rightMargin: Tokens.pagePadding
                Layout.topMargin: Tokens.s5
                Layout.bottomMargin: Tokens.s2
                spacing: Tokens.s2

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Tokens.s6

                    Label {
                        text: "‹"
                        visible: contentStack.depth > 1
                        color: Theme.muted
                        font.pixelSize: 18
                        MouseArea {
                            anchors.fill: parent
                            anchors.margins: -6   // a comfortable hit target
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                contentStack.pop();
                                shell.currentTitle =
                                    shell.destinations[shell.currentIndex].label;
                            }
                        }
                    }
                    Label {
                        text: shell.headerTitle
                        color: Theme.onSurface
                        font.pixelSize: Tokens.textTitle
                        font.bold: true
                        elide: Text.ElideRight
                    }
                    Rectangle {
                        visible: shell.headerPill.length > 0
                        implicitWidth: pillText.implicitWidth + 16
                        implicitHeight: pillText.implicitHeight + 6
                        radius: Tokens.radiusChip
                        color: Theme.primaryFill
                        border.width: 1
                        border.color: Theme.outline

                        Text {
                            id: pillText
                            anchors.centerIn: parent
                            text: shell.headerPill
                            font.family: Tokens.monoFamily
                            font.pixelSize: 9
                            font.letterSpacing: 1
                            color: Theme.primary
                        }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    visible: shell.headerSub.length > 0
                    spacing: 7

                    Kit.StatusDot {
                        token: shell.headerDot
                        visible: shell.headerDot.length > 0
                    }
                    Label {
                        text: shell.headerSub
                        color: Theme.muted
                        font.pixelSize: Tokens.textSummary
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
            }

            // The content host. Transparent: pages sit on the window surface.
            StackView {
                id: contentStack
                Layout.fillWidth: true
                Layout.fillHeight: true
                background: null
                // Pages reach the shell (destination switch, detail push)
                // through this — `page.StackView.view.shellApi`.
                readonly property var shellApi: shell
                // Confine the slide transition to the content bounds so an
                // outgoing page disappears at the rail divider (slides "behind"
                // it) instead of painting over the rail to the edge.
                clip: true
                // Push the first page AFTER the StackView has a layout size rather
                // than via initialItem: initialItem is created during construction
                // when this is still 0x0 inside the Layout, so the first page (the
                // default Controllers destination) renders blank until a later
                // navigation forces a relayout. Deferring to onCompleted creates it
                // already sized.
                Component.onCompleted: contentStack.push(Qt.resolvedUrl(shell.destinations[0].source))
            }
        }
    }

    // ---- Global transient-notification host. Dropped ONCE here so every
    // one-shot failure in the app (a failed connect/reconnect, an external-link
    // open that couldn't launch, a USB path-switch notice) surfaces as a toast
    // instead of being silently dropped. It overlays the whole shell but only
    // paints at the bottom-center, so it never blocks interaction elsewhere.
    Kit.NotificationToastHost {
        id: toastHost
        anchors.fill: parent
    }
    Connections {
        target: App
        function onErrorMessage(message) { toastHost.show(message); }
    }
}
