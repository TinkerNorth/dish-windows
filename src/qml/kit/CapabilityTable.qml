// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The four-layer capability matrix — the one place the vocabulary
// "available = controller ∩ transport ∩ type ∩ host" is drawn. Two callers:
// the wizard's type cards (compact) and Configure binding's WHAT CARRIES panel.
//
// `rows` is the QVariantList App.capabilityForCandidate() returns, each row
// augmented by the CALLER with { name, why }. This component never asks the
// domain anything — it takes rows and draws them.
//
// Two rules the mocks got wrong and this component fixes:
//  * Every layer cell shows its TRUE state. The "name only the first failing
//    layer" rule applies to the sentence, not the cells; a later failure drawn
//    neutral reads as a pass, and the user fixes the wrong thing. The first
//    failure is heavier instead.
//  * A Pending row draws a dash in all four cells, never a cross. An
//    unresolved catalog is "we do not know yet", and a guessed "unsupported"
//    is worse than no table.

// Bound: the row delegate reads the outer `table` id and nests a cell Repeater.
pragma ComponentBehavior: Bound

import QtQuick
import Dish.Chrome

Item {
    id: table

    // [{ feature, inOk, linkOk, typeOk, hostOk, verdict, failingLayer,
    //    hasFailingLayer, name, why }]
    property var rows: []
    property bool showHeader: true
    // The wizard's type card: no per-row why line, tighter rows.
    property bool compact: false

    readonly property int layerColumnWidth: 40
    readonly property int verdictColumnWidth: 66
    // The 292px panel cannot carry a name column AND four 40px layer columns AND
    // the verdict word on one line — the names elide to three letters. So the
    // full form puts the layers on their own labelled line under the name (SCR
    // 6.6's own layout); only the wizard's compact table keeps aligned columns,
    // where the shared header is the point.
    readonly property bool stacked: !table.compact

    implicitWidth: 60 + 4 * table.layerColumnWidth + table.verdictColumnWidth
    implicitHeight: column.implicitHeight

    // Four verdicts, four words. "Off" means the user turned it off and nothing
    // else — a pad with no gyro reading "Off" sends them hunting for a switch.
    function verdictText(v) {
        if (v === "available")
            return qsTr("Available");
        if (v === "unavailable")
            return qsTr("Unavailable");
        if (v === "off")
            return qsTr("Off");
        return qsTr("Pending");
    }

    function verdictColor(v) {
        return v === "available" ? Theme.success : Theme.mutedStrong;
    }

    // The spoken form of one cell, for the per-row description.
    function cellStateText(ok, pending) {
        if (pending)
            return qsTr("unknown");
        return ok ? qsTr("yes") : qsTr("no");
    }

    // "Gyro: Unavailable. In yes, Link no, Type yes, Host no. Standard mode
    //  can't carry it — switch the connection to Direct."
    function rowDescription(row) {
        if (!row)
            return "";
        const pending = row.verdict === "pending";
        const layers = [
            qsTr("In %1").arg(table.cellStateText(row.inOk === true, pending)),
            qsTr("Link %1").arg(table.cellStateText(row.linkOk === true, pending)),
            qsTr("Type %1").arg(table.cellStateText(row.typeOk === true, pending)),
            qsTr("Host %1").arg(table.cellStateText(row.hostOk === true, pending))
        ].join(", ");
        const why = row.why !== undefined && row.why.length > 0 ? " " + row.why : "";
        return qsTr("%1: %2.").arg(row.name !== undefined ? row.name : "")
                              .arg(table.verdictText(row.verdict)) + " " + layers + "." + why;
    }

    Column {
        id: column
        width: table.width
        spacing: 0

        // ── Header ───────────────────────────────────────────────────────────
        Item {
            id: header
            visible: table.showHeader
            width: parent.width
            height: visible ? carries.implicitHeight + 2 * Tokens.s3 : 0

            Eyebrow {
                id: carries
                mutedTone: true
                text: qsTr("Carries")
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
            }

            Row {
                id: headerCells
                visible: !table.stacked
                anchors.right: parent.right
                anchors.rightMargin: table.verdictColumnWidth
                anchors.verticalCenter: parent.verticalCenter
                spacing: 0

                Eyebrow {
                    mutedTone: true
                    text: qsTr("In")
                    width: table.layerColumnWidth
                    horizontalAlignment: Text.AlignHCenter
                }
                Eyebrow {
                    mutedTone: true
                    text: qsTr("Link")
                    width: table.layerColumnWidth
                    horizontalAlignment: Text.AlignHCenter
                }
                Eyebrow {
                    mutedTone: true
                    text: qsTr("Type")
                    width: table.layerColumnWidth
                    horizontalAlignment: Text.AlignHCenter
                }
                Eyebrow {
                    mutedTone: true
                    text: qsTr("Host")
                    width: table.layerColumnWidth
                    horizontalAlignment: Text.AlignHCenter
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.outlineSubtle
            }
        }

        // ── Rows ─────────────────────────────────────────────────────────────
        Repeater {
            model: table.rows

            delegate: Item {
                id: capRow

                required property var modelData

                readonly property string verdict: capRow.modelData.verdict !== undefined
                                                ? capRow.modelData.verdict : "pending"
                readonly property bool pending: capRow.verdict === "pending"
                readonly property string failing: capRow.modelData.hasFailingLayer === true
                                                  && capRow.modelData.failingLayer !== undefined
                                                ? capRow.modelData.failingLayer : ""
                readonly property string why: !table.compact
                                              && capRow.modelData.why !== undefined
                                            ? capRow.modelData.why : ""
                // The four cells, left to right, each with the "fix this one
                // first" flag the sentence below repeats in words.
                readonly property var layerCells: [
                    { "label": qsTr("In"), "ok": capRow.modelData.inOk === true,
                      "heavy": capRow.failing === "input" },
                    { "label": qsTr("Link"), "ok": capRow.modelData.linkOk === true,
                      "heavy": capRow.failing === "link" },
                    { "label": qsTr("Type"), "ok": capRow.modelData.typeOk === true,
                      "heavy": capRow.failing === "type" },
                    { "label": qsTr("Host"), "ok": capRow.modelData.hostOk === true,
                      "heavy": capRow.failing === "host" }
                ]

                function markText(ok) {
                    return capRow.pending ? "—" : ok ? "✓" : "✕";
                }

                function markColor(ok) {
                    if (capRow.pending)
                        return Theme.mutedStrong;
                    return ok ? Theme.success : Theme.error;
                }

                width: column.width
                height: content.implicitHeight + 2 * (table.compact ? Tokens.s3 : Tokens.s4)

                Accessible.role: Accessible.StaticText
                Accessible.name: qsTr("%1: %2")
                                 .arg(capRow.modelData.name !== undefined
                                      ? capRow.modelData.name : "")
                                 .arg(table.verdictText(capRow.verdict))
                Accessible.description: table.rowDescription(capRow.modelData)

                Column {
                    id: content
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Tokens.s1

                    Item {
                        id: topLine
                        width: parent.width
                        height: Math.max(nameText.implicitHeight, verdictLabel.implicitHeight,
                                         cells.implicitHeight)

                        Text {
                            id: nameText
                            anchors.left: parent.left
                            anchors.right: table.stacked ? verdictLabel.left : cells.left
                            anchors.rightMargin: Tokens.s4
                            anchors.verticalCenter: parent.verticalCenter
                            text: capRow.modelData.name !== undefined ? capRow.modelData.name : ""
                            elide: Text.ElideRight
                            font.pixelSize: Tokens.textSummary
                            // Full opacity even when unavailable: the label is
                            // the part the user has to read.
                            color: capRow.verdict === "available" ? Theme.onSurface
                                                                  : Theme.mutedStrong
                        }

                        Row {
                            id: cells
                            visible: !table.stacked
                            anchors.right: verdictLabel.left
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 0

                            Repeater {
                                model: capRow.layerCells

                                delegate: Text {
                                    id: cell
                                    required property var modelData

                                    width: table.layerColumnWidth
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: Tokens.textMeta
                                    font.weight: cell.modelData.heavy && !capRow.pending
                                               ? Font.DemiBold : Font.Normal
                                    text: capRow.markText(cell.modelData.ok)
                                    color: capRow.markColor(cell.modelData.ok)
                                }
                            }
                        }

                        Text {
                            id: verdictLabel
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            width: table.verdictColumnWidth
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            text: table.verdictText(capRow.verdict)
                            color: table.verdictColor(capRow.verdict)
                            font.family: Tokens.monoFamily
                            font.pixelSize: Tokens.textChip
                        }
                    }

                    // The layers, labelled, on their own line — the full-width
                    // form. Every cell still shows its TRUE state; only the
                    // first failure is heavier.
                    Row {
                        id: stackedCells
                        visible: table.stacked
                        spacing: Tokens.s5

                        Repeater {
                            model: capRow.layerCells

                            delegate: Row {
                                id: layerChip
                                required property var modelData

                                spacing: Tokens.s2

                                Text {
                                    text: layerChip.modelData.label
                                    font.family: Tokens.monoFamily
                                    font.pixelSize: Tokens.textChip
                                    font.letterSpacing: Tokens.sectionLetterSpacing
                                    font.capitalization: Font.AllUppercase
                                    color: Theme.mutedStrong
                                }
                                Text {
                                    text: capRow.markText(layerChip.modelData.ok)
                                    font.pixelSize: Tokens.textChip
                                    font.weight: layerChip.modelData.heavy && !capRow.pending
                                               ? Font.DemiBold : Font.Normal
                                    color: capRow.markColor(layerChip.modelData.ok)
                                }
                            }
                        }
                    }

                    Text {
                        id: whyText
                        visible: capRow.why.length > 0
                        width: parent.width
                        text: capRow.why
                        wrapMode: Text.WordWrap
                        font.pixelSize: Tokens.textMeta
                        color: Theme.mutedStrong
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Theme.outlineSubtle
                }
            }
        }
    }
}
