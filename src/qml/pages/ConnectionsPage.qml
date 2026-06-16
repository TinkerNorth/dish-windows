// SPDX-License-Identifier: LGPL-3.0-or-later
// Copyright (C) 2026 Dish contributors.
//
// PLACEHOLDER. The Connections destination — a later page agent replaces the
// body with the connection list (App.connectionModel) + discovery (App.start
// Discovery / discoveredServers). Proves navigation only for now.

import QtQuick.Controls.Basic
import "../kit" as Kit
import Dish.Chrome

Kit.Page {
    title: qsTr("Connections")

    Kit.SectionHeader { label: qsTr("Connections") }

    Label {
        text: qsTr("TODO: connections list (App.connectionModel)")
        color: Theme.muted
        font.pixelSize: 13
    }
}
