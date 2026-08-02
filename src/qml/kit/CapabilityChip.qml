// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// THE pill. Six tones, zero hand-rolls: the accent capability pill, the
// outlined "not available" pill, the amber low-battery pill, the green trust
// badge ("Verified", "Best fit"), the amber warning badge ("Layout guessed")
// and the neutral identity pill ("as DualSense") are all this component. Adding
// a second pill style is a review-blocking change.
//
// It ALWAYS renders. An absent capability draws the outlined pill with the
// negated label ("No gyro") at FULL opacity — status is always drawn, never
// merely absent, and the outline is the visual cue, not a fade.

import QtQuick
import Dish.Chrome

Rectangle {
    id: chip

    enum Tone { Present, Absent, Low, Ok, Warn, Neutral }

    property string text: ""
    property int tone: CapabilityChip.Present

    implicitWidth: label.implicitWidth + Tokens.s7
    implicitHeight: label.implicitHeight + Tokens.s2
    radius: Tokens.radiusChip

    color: chip.tone === CapabilityChip.Absent ? "transparent"
         : chip.tone === CapabilityChip.Low ? Theme.warningFill
         : chip.tone === CapabilityChip.Ok ? Theme.successFill
         : chip.tone === CapabilityChip.Warn ? Theme.warningFill
         : chip.tone === CapabilityChip.Neutral ? Theme.surfaceDim
         : Theme.primaryFill

    // Only the absent tone carries a border; a 1px transparent border would cut
    // a hole in the fill rather than being invisible.
    border.width: chip.tone === CapabilityChip.Absent ? 1 : 0
    border.color: Theme.muted

    Accessible.role: Accessible.StaticText
    Accessible.name: chip.text

    Text {
        id: label
        anchors.centerIn: parent
        text: chip.text
        font.pixelSize: Tokens.textChip
        font.weight: chip.tone === CapabilityChip.Low ? Font.DemiBold : Font.Medium
        color: chip.tone === CapabilityChip.Absent ? Theme.mutedStrong
             : chip.tone === CapabilityChip.Low ? Theme.warning
             : chip.tone === CapabilityChip.Ok ? Theme.success
             : chip.tone === CapabilityChip.Warn ? Theme.warning
             : chip.tone === CapabilityChip.Neutral ? Theme.mutedStrong
             : Theme.primary
    }
}
