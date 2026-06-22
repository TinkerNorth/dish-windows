// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// The shared bottom navigation row for an onboarding screen: Back (left) and a
// trailing Skip + primary Next/Finish. Each screen wires the three signals to
// its own paging; this component holds no paging state of its own — it just
// renders the buttons and forwards clicks. Visibility of Back/Skip and the
// primary label are driven by the host screen via the exposed properties.

import QtQuick
import QtQuick.Layouts
import "../kit" as Kit

RowLayout {
    id: nav

    spacing: 12

    // Host screen toggles these per step.
    property bool backVisible: true
    property bool skipVisible: true
    property string primaryText: qsTr("Next")

    signal backClicked()
    signal skipClicked()
    signal primaryClicked()

    Kit.OutlineButton {
        text: qsTr("Back")
        visible: nav.backVisible
        onClicked: nav.backClicked()
    }

    Item { Layout.fillWidth: true }

    Kit.OutlineButton {
        text: qsTr("Skip")
        visible: nav.skipVisible
        onClicked: nav.skipClicked()
    }

    Kit.KitButton {
        text: nav.primaryText
        onClicked: nav.primaryClicked()
    }
}
