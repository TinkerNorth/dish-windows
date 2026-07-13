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

    // Emitted when the user taps Retry on the error banner; the page re-kicks the
    // catalog fetch (App.refreshEmulate) for the slot it is showing.
    signal retryRequested()

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

    body: [
        Label {
            text: qsTr("Choose how this controller appears to the host.")
            color: Theme.muted
            font.pixelSize: 12
            Layout.fillWidth: true
        },
        // ── The catalog fetch's four states, each rendered distinctly (was a
        // single static hint that conflated loading / empty / error). All bind
        // App's AsyncState projection so they clear reactively when the GET lands. ──
        Kit.LoadingSpinner {
            // In-flight with nothing cached yet → a spinner, not a blank list.
            visible: App.emulateLoading
            text: qsTr("Loading controller types…")
            Layout.fillWidth: true
        },
        Kit.ErrorBanner {
            // The fetch failed: show the typed reason + a Retry. If we have stale
            // cached types they still render below; this banner sits above them.
            visible: App.emulateError.length > 0
            text: App.emulateError
            showRetry: true
            onRetryRequested: picker.retryRequested()
            Layout.fillWidth: true
        },
        Label {
            // Stale-while-revalidate cue: we're showing cached types from a prior
            // fetch while a background refresh is in flight (App.emulateStale).
            // Distinct from the cold-load spinner above (which shows only when there
            // is nothing cached yet). Binds the previously-unread emulateStale.
            visible: App.emulateStale && picker.types.length > 0 && App.emulateError.length === 0
            text: qsTr("Updating controller types…")
            color: Theme.muted
            font.pixelSize: 11
            font.italic: true
            Layout.fillWidth: true
        },
        Label {
            // Genuinely empty: not loading, no error, and the catalog offered
            // nothing (or the slot is unbound) — distinct from the two states above.
            visible: !App.emulateLoading && App.emulateError.length === 0
                     && picker.types.length === 0
            text: qsTr("No controller types available for this connection.")
            color: Theme.muted
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        },
        ListView {
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
