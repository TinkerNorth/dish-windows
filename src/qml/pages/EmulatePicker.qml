// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The controller-type ("Emulate") picker for a bound slot. A ContentDialog that
// lists the offerable controller types for a slot and emits the chosen wire type
// on accept. The page drives it: it calls load(types, current) with the result
// of App.emulateTypes()/App.emulateCurrentType() and applies the choice via
// App.setControllerType() in its `onChosen` handler. This file holds NO business
// logic — it only presents the contract's type objects and reports a selection.

// Bound so the nested delegate may reference the picker id without an
// unqualified-access warning.
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "../kit" as Kit
import Dish.Chrome

Kit.ContentDialog {
    id: picker

    heading: qsTr("Emulate controller")
    acceptText: qsTr("Apply")
    // Nothing to apply until a type is highlighted.
    acceptEnabled: typeListView.currentIndex >= 0

    // The list of {type,slug,name,shortName,description,known} objects from
    // App.emulateTypes(); a plain JS array driven into the ListView model.
    property var types: []

    // Emitted on accept with the chosen wire `type` (int). The page maps this to
    // App.setControllerType(slotId, type).
    signal chosen(int type)

    // Populate the picker and pre-select the current wire type. Called by the
    // page just before open() so the list is fresh each time.
    function load(offered, currentType) {
        picker.types = offered;
        // Pre-select the row whose `type` matches the current wire type; fall
        // back to no selection if it isn't offered.
        var sel = -1;
        for (var i = 0; i < picker.types.length; ++i) {
            if (picker.types[i].type === currentType) { sel = i; break; }
        }
        typeListView.currentIndex = sel;
    }

    // contentColumn is a frozen Kit.ContentDialog alias (QML_UI_KIT.md §4);
    // the linter cannot see the alias target's children list (known limit).
    contentColumn.children: [
        Label { // qmllint disable missing-property
            text: qsTr("Choose how this controller appears to the host.")
            color: Theme.muted
            font.pixelSize: 12
            Layout.fillWidth: true
        },
        Label { // qmllint disable missing-property
            // Empty-offer hint: the slot may be unbound or its catalog not cached
            // yet (the page kicks refreshEmulate before opening, but the first
            // open can still race an empty catalog).
            visible: picker.types.length === 0
            text: qsTr("No controller types available yet — try again in a moment.")
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        ListView { // qmllint disable missing-property
            id: typeListView
            visible: picker.types.length > 0
            Layout.fillWidth: true
            implicitHeight: Math.min(contentHeight, 280)
            clip: true
            spacing: 4
            currentIndex: -1
            model: picker.types

            delegate: ItemDelegate {
                id: row
                required property int index
                required property var modelData
                width: ListView.view ? ListView.view.width : implicitWidth
                highlighted: ListView.isCurrentItem
                onClicked: typeListView.currentIndex = row.index

                contentItem: ColumnLayout {
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Label {
                            text: row.modelData.name
                            color: Theme.onSurface
                            font.pixelSize: 13
                            font.bold: row.highlighted
                        }
                        Item { Layout.fillWidth: true }
                        // "known" marks a recognised hardware class; unknown types
                        // are still offerable but flagged so the choice is informed.
                        Label {
                            visible: !row.modelData.known
                            text: qsTr("unverified")
                            color: Theme.warning
                            font.pixelSize: 10
                        }
                    }
                    Label {
                        visible: row.modelData.description.length > 0
                        text: row.modelData.description
                        color: Theme.muted
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }

                background: Rectangle {
                    radius: 8
                    color: row.highlighted
                               ? Qt.rgba(Theme.primary.r, Theme.primary.g, Theme.primary.b, 0.18)
                         : row.hovered
                               ? Qt.rgba(Theme.onSurface.r, Theme.onSurface.g, Theme.onSurface.b, 0.06)
                         : "transparent"
                }
            }
        }
    ]

    onAccepted: {
        if (typeListView.currentIndex < 0) { return; }
        picker.chosen(picker.types[typeListView.currentIndex].type);
        // The page closes on success (it may keep us open on error); no close here.
    }
}
