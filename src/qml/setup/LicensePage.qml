// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// Step 1: the licence texts, honestly framed. There is deliberately NO
// "I agree" gate: the LGPL-3.0 requires the text to accompany the software,
// not a click-through — free software grants rights, and forcing agreement
// would be both legally pointless and off-voice. The page is a reading
// surface; Continue is always enabled. Legal texts ship verbatim and are
// never translated.

import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import Dish.Chrome
import Dish.Chrome as Kit
import Dish.Setup

Item {
    id: page

    readonly property string hint: ""
    readonly property string primaryLabel: qsTr("Continue ›")
    readonly property bool canAdvance: true

    implicitHeight: pageColumn.implicitHeight

    Accessible.name: heading.text + " — " + sub.text

    property int segIndex: 0
    readonly property var segments: [qsTr("LGPL-3.0"), qsTr("GPL-3.0"), qsTr("Third-party")]

    // Resolved once; the rows are embedded resources and never change.
    property string lgplText: ""
    property string gplText: ""
    property var thirdPartyRows: []

    Component.onCompleted: {
        const rows = Setup.licenseEntries();
        const third = [];
        for (let i = 0; i < rows.length; ++i) {
            const row = rows[i];
            if (row.id === "lgpl3")
                page.lgplText = row.text;
            else if (row.id === "gpl3")
                page.gplText = row.text;
            else
                third.push(row);
        }
        page.thirdPartyRows = third;
    }

    function activated() {
        page.resetScroll();
    }

    function resetScroll() {
        scroll.ScrollBar.vertical.position = 0;
    }

    function focusFirst() {
        seg.forceActiveFocus(Qt.TabFocusReason);
        return true;
    }

    ColumnLayout {
        id: pageColumn
        anchors.fill: parent
        spacing: Tokens.s6

        ColumnLayout {
            spacing: Tokens.s1
            Layout.fillWidth: true

            Label {
                id: heading
                text: qsTr("Free software")
                color: Theme.onSurface
                font.pixelSize: Tokens.textStatus
                font.bold: true
                wrapMode: Text.WordWrap
                Accessible.role: Accessible.Heading
                Layout.fillWidth: true
            }
            Label {
                id: sub
                text: qsTr("Dish ships under the LGPL-3.0, with the GPL-3.0 and third-party notices beside it. All of it installs next to the app; nothing here asks for agreement.")
                color: Theme.muted
                font.pixelSize: Tokens.textSummary
                lineHeight: 1.5
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }

        Kit.SegmentedControl {
            id: seg
            options: page.segments
            value: page.segments[page.segIndex]
            onPicked: function (option) {
                const i = page.segments.indexOf(option);
                if (i >= 0) {
                    page.segIndex = i;
                    page.resetScroll();
                }
            }
        }

        // The reading well: a recessed mono surface, never a Card — it is a
        // document, not a panel.
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Tokens.radiusCard
            color: Theme.surfaceDim
            border.width: 1
            border.color: Theme.outline

            ScrollView {
                id: scroll
                anchors.fill: parent
                anchors.leftMargin: Tokens.s7
                anchors.rightMargin: Tokens.s7
                anchors.topMargin: Tokens.s6
                anchors.bottomMargin: Tokens.s6
                contentWidth: availableWidth
                clip: true

                Column {
                    width: scroll.availableWidth

                    TextArea {
                        visible: page.segIndex !== 2
                        width: parent.width
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.Wrap
                        textFormat: TextEdit.PlainText
                        text: page.segIndex === 0 ? page.lgplText : page.gplText
                        color: Theme.onSurface
                        font.family: Tokens.monoFamily
                        font.pixelSize: Tokens.textMeta
                        background: null
                    }

                    Column {
                        visible: page.segIndex === 2
                        width: parent.width
                        spacing: Tokens.s8

                        Repeater {
                            model: page.thirdPartyRows

                            delegate: Column {
                                id: entry

                                required property var modelData

                                width: parent.width
                                spacing: Tokens.s2

                                Kit.SectionHeader {
                                    label: entry.modelData.name + " " + entry.modelData.version
                                }
                                Text {
                                    width: parent.width
                                    text: entry.modelData.spdx
                                    color: Theme.mutedStrong
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: Tokens.textChip
                                }
                                Text {
                                    width: parent.width
                                    text: entry.modelData.text
                                    wrapMode: Text.Wrap
                                    color: Theme.onSurface
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: Tokens.textMeta
                                    lineHeight: 1.45
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
